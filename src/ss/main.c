// Storage Server (SS): prepares local storage dir, registers to NM,
// and sends periodic heartbeats.
// Phase 2: Now includes file scanning and storage management.
#include <arpa/inet.h>
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
    int nm_fd;           // Connection TO NM (for registration/heartbeat)
    int server_fd;      // Server socket listening on client_port (for commands from NM)
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

// Command handler thread - processes commands from NM
static void *cmd_thread(void *arg) {
    Ctx *ctx = (Ctx*)arg;
    
    // Set up server socket to listen for commands from NM
    int server_fd = create_server_socket(ctx->host, ctx->client_port);
    if (server_fd < 0) {
        log_error("ss_server_socket", "failed to create server socket on %s:%d", ctx->host, ctx->client_port);
        return NULL;
    }
    ctx->server_fd = server_fd;
    log_info("ss_listen", "listening on %s:%d for commands", ctx->host, ctx->client_port);
    
    while (ctx->running) {
        // Accept connection from NM
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
        if (client_fd < 0) {
            if (ctx->running) continue;
            break;
        }
        
        // Handle command on this connection
        char cmd_line[MAX_LINE];
        int n = recv_line(client_fd, cmd_line, sizeof(cmd_line));
        if (n <= 0) {
            close(client_fd);
            continue;
        }
        
        // Parse command message
        Message cmd_msg;
        if (proto_parse_line(cmd_line, &cmd_msg) != 0) {
            log_error("ss_parse_error", "failed to parse command");
            close(client_fd);
            continue;
        }
        
        // Handle CREATE command
        if (strcmp(cmd_msg.type, "CREATE") == 0) {
            const char *filename = cmd_msg.payload;
            const char *owner = cmd_msg.username;
            
            log_info("ss_cmd_create", "file=%s owner=%s", filename, owner);
            
            // Create file
            if (file_create(ctx->storage_dir, filename, owner) == 0) {
                // Send ACK
                Message ack = {0};
                (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
                (void)snprintf(ack.id, sizeof(ack.id), "%s", cmd_msg.id);
                (void)snprintf(ack.username, sizeof(ack.username), "%s", cmd_msg.username);
                (void)snprintf(ack.role, sizeof(ack.role), "%s", "SS");
                (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "created");
                
                char ack_line[MAX_LINE];
                proto_format_line(&ack, ack_line, sizeof(ack_line));
                send_all(client_fd, ack_line, strlen(ack_line));
                
                log_info("ss_file_created", "file=%s", filename);
            } else {
                // Send error (file already exists or other error)
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                  "CONFLICT", "File already exists or creation failed",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                log_error("ss_create_failed", "file=%s", filename);
            }
        }
        // Handle DELETE command
        else if (strcmp(cmd_msg.type, "DELETE") == 0) {
            const char *filename = cmd_msg.payload;
            
            log_info("ss_cmd_delete", "file=%s", filename);
            
            // Delete file
            if (file_delete(ctx->storage_dir, filename) == 0) {
                // Send ACK
                Message ack = {0};
                (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
                (void)snprintf(ack.id, sizeof(ack.id), "%s", cmd_msg.id);
                (void)snprintf(ack.username, sizeof(ack.username), "%s", cmd_msg.username);
                (void)snprintf(ack.role, sizeof(ack.role), "%s", "SS");
                (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "deleted");
                
                char ack_line[MAX_LINE];
                proto_format_line(&ack, ack_line, sizeof(ack_line));
                send_all(client_fd, ack_line, strlen(ack_line));
                
                log_info("ss_file_deleted", "file=%s", filename);
            } else {
                // Send error (file not found or other error)
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                  "NOT_FOUND", "File not found or deletion failed",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                log_error("ss_delete_failed", "file=%s", filename);
            }
        }
        // Handle GETMETA command (get file metadata)
        else if (strcmp(cmd_msg.type, "GETMETA") == 0) {
            const char *filename = cmd_msg.payload;
            
            log_info("ss_cmd_getmeta", "file=%s", filename);
            
            // Load metadata from disk
            FileMetadata meta;
            if (metadata_load(ctx->storage_dir, filename, &meta) == 0) {
                // Send metadata in DATA response
                Message data_resp = {0};
                (void)snprintf(data_resp.type, sizeof(data_resp.type), "%s", "DATA");
                (void)snprintf(data_resp.id, sizeof(data_resp.id), "%s", cmd_msg.id);
                (void)snprintf(data_resp.username, sizeof(data_resp.username), "%s", cmd_msg.username);
                (void)snprintf(data_resp.role, sizeof(data_resp.role), "%s", "SS");
                
                // Format metadata as: "owner=alice,size=100,words=50,chars=200"
                (void)snprintf(data_resp.payload, sizeof(data_resp.payload),
                              "owner=%s,size=%zu,words=%d,chars=%d",
                              meta.owner, meta.size_bytes, meta.word_count, meta.char_count);
                
                char resp_line[MAX_LINE];
                proto_format_line(&data_resp, resp_line, sizeof(resp_line));
                send_all(client_fd, resp_line, strlen(resp_line));
                
                log_info("ss_metadata_sent", "file=%s owner=%s", filename, meta.owner);
            } else {
                // Send error (metadata not found)
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                  "NOT_FOUND", "Metadata not found",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                log_error("ss_getmeta_failed", "file=%s", filename);
            }
        }
        // Unknown command
        else {
            log_error("ss_unknown_cmd", "type=%s", cmd_msg.type);
            char error_buf[MAX_LINE];
            proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                              "INVALID", "Unknown command",
                              error_buf, sizeof(error_buf));
            send_all(client_fd, error_buf, strlen(error_buf));
        }
        
        close(client_fd);
    }
    
    close(server_fd);
    return NULL;
}

int main(int argc, char **argv) {
    Ctx ctx = {0};
    ctx.nm_host = "127.0.0.1"; ctx.nm_port = 5000; ctx.host = "127.0.0.1"; ctx.client_port = 6001; ctx.storage_dir = "./storage_ss1"; ctx.username = "ss1"; ctx.running = 1;
    ctx.server_fd = -1;  // Initialize server_fd
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
    
    // Start heartbeat thread (sends heartbeats to NM)
    pthread_t hb_th; (void)pthread_create(&hb_th, NULL, hb_thread, &ctx);
    
    // Start command handler thread (listens for commands from NM on client_port)
    pthread_t cmd_th; (void)pthread_create(&cmd_th, NULL, cmd_thread, &ctx);
    
    // Wait for threads to finish
    log_info("ss_ready", "SS running - heartbeat and command handler active");
    while (ctx.running) {
        sleep(1);
    }
    
    ctx.running = 0;
    pthread_join(hb_th, NULL);
    pthread_join(cmd_th, NULL);
    close(ctx.nm_fd);
    if (ctx.server_fd >= 0) {
        close(ctx.server_fd);
    }
    return 0;
}


