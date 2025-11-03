// Storage Server (SS): prepares local storage dir, registers to NM,
// and sends periodic heartbeats.
// Phase 2: Now includes file scanning and storage management.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/net.h"
#include "../common/log.h"
#include "../common/protocol.h"
#include "file_scan.h"
#include "file_storage.h"

typedef struct {
    const char *nm_host;
    int nm_port;
    const char *host;
    int client_port;
    const char *storage_dir;
    const char *username;
    int nm_fd;
    int running;
} Ctx;

// Ensure storage directory exists and has proper structure
// Phase 2: Uses file_storage functions which handle directory creation
static void ensure_storage_dir(const char *path) {
    // Create base directory and subdirectories (files/, metadata/)
    // This is done by file_storage functions, but we ensure base dir exists
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
    int rc = system(cmd);
    if (rc == -1) {
        log_error("ss_storage_dir", "failed to create dir: %s", path);
    }
    // Subdirectories (files/, metadata/) will be created by file_storage functions when needed
}

// Periodic heartbeat sender to NM.
static void *hb_thread(void *arg) {
    Ctx *ctx = (Ctx*)arg;
    int seq = 0;
    while (ctx->running) {
        Message hb = {0};
        (void)snprintf(hb.type, sizeof(hb.type), "%s", "HEARTBEAT");
        (void)snprintf(hb.id, sizeof(hb.id), "hb-%d", seq++);
        (void)snprintf(hb.username, sizeof(hb.username), "%s", ctx->username);
        (void)snprintf(hb.role, sizeof(hb.role), "%s", "SS");
        hb.payload[0] = '\0';
        char line[MAX_LINE]; proto_format_line(&hb, line, sizeof(line));
        if (send_all(ctx->nm_fd, line, strlen(line)) != 0) {
            log_error("ss_hb_send", "lost nm connection");
            break;
        }
        sleep(5);
    }
    return NULL;
}

int main(int argc, char **argv) {
    Ctx ctx = {0};
    ctx.nm_host = "127.0.0.1"; ctx.nm_port = 5000; ctx.host = "127.0.0.1"; ctx.client_port = 6001; ctx.storage_dir = "./storage_ss1"; ctx.username = "ss1"; ctx.running = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--nm-host") && i+1 < argc) ctx.nm_host = argv[++i];
        else if (!strcmp(argv[i], "--nm-port") && i+1 < argc) ctx.nm_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--host") && i+1 < argc) ctx.host = argv[++i];
        else if (!strcmp(argv[i], "--client-port") && i+1 < argc) ctx.client_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--storage") && i+1 < argc) ctx.storage_dir = argv[++i];
        else if (!strcmp(argv[i], "--username") && i+1 < argc) ctx.username = argv[++i];
    }
    // Ensure storage directory exists
    ensure_storage_dir(ctx.storage_dir);
    
    // Phase 2: Scan directory for existing files
    // This discovers all files that were created before SS restart
    // The file list will be sent to NM during registration
    log_info("ss_scan_start", "scanning storage directory: %s", ctx.storage_dir);
    ScanResult scan_result = scan_directory(ctx.storage_dir, "files");
    log_info("ss_scan_complete", "found %d files", scan_result.count);
    
    // Build file list string for registration payload
    // Format: "host=IP,client_port=PORT,storage=DIR,files=file1.txt,file2.txt,..."
    char file_list[4096];
    if (build_file_list_string(&scan_result, file_list, sizeof(file_list)) != 0) {
        log_error("ss_scan_error", "file list too large, truncating");
        file_list[0] = '\0';  // Empty list if too many files
    }
    
    // Connect to NM
    ctx.nm_fd = connect_to_host(ctx.nm_host, ctx.nm_port);
    if (ctx.nm_fd < 0) { perror("connect nm"); return 1; }
    
    // Register to NM with file list
    Message reg = {0};
    (void)snprintf(reg.type, sizeof(reg.type), "%s", "SS_REGISTER");
    (void)snprintf(reg.id, sizeof(reg.id), "%s", "1");
    (void)snprintf(reg.username, sizeof(reg.username), "%s", ctx.username);
    (void)snprintf(reg.role, sizeof(reg.role), "%s", "SS");
    
    // Build registration payload: host, client_port, storage_dir, and file list
    // Note: reg.payload is limited to 1792 bytes, so we may need to truncate file list
    char payload[4096];
    if (scan_result.count > 0 && file_list[0] != '\0') {
        snprintf(payload, sizeof(payload), "host=%s,client_port=%d,storage=%s,files=%s",
                 ctx.host, ctx.client_port, ctx.storage_dir, file_list);
    } else {
        // No files found, just send basic info
        snprintf(payload, sizeof(payload), "host=%s,client_port=%d,storage=%s,files=",
                 ctx.host, ctx.client_port, ctx.storage_dir);
    }
    // Truncate payload to fit in reg.payload (1792 bytes)
    size_t payload_len = strlen(payload);
    if (payload_len >= sizeof(reg.payload)) {
        payload_len = sizeof(reg.payload) - 1;
        log_error("ss_payload_trunc", "payload truncated to %zu bytes", payload_len);
    }
    memcpy(reg.payload, payload, payload_len);
    reg.payload[payload_len] = '\0';
    
    // Send registration message
    char line[MAX_LINE]; proto_format_line(&reg, line, sizeof(line));
    send_all(ctx.nm_fd, line, strlen(line));
    
    // Wait for ACK from NM
    char rbuf[MAX_LINE]; recv_line(ctx.nm_fd, rbuf, sizeof(rbuf));
    log_info("ss_registered", "payload=%s", payload);
    // Start heartbeat thread
    pthread_t th; (void)pthread_create(&th, NULL, hb_thread, &ctx);
    // Keep connection open; Phase 1 has no further commands
    while (1) { sleep(60); }
    ctx.running = 0; pthread_join(th, NULL); close(ctx.nm_fd);
    return 0;
}


