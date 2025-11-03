// Name Server (NM): accepts connections from SS/Clients and handles
// registration and heartbeats in Phase 1. Thread-per-connection model.
#define _POSIX_C_SOURCE 200809L  // For strdup
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/net.h"
#include "../common/log.h"
#include "../common/protocol.h"
#include "index.h"
#include "access_control.h"
#include "commands.h"
#include "registry.h"

// Argument passed to each connection handler thread.
typedef struct ClientConnArg {
    int fd;
    struct sockaddr_in addr;
} ClientConnArg;

static volatile int g_running = 1;

// Handle a single parsed message from a peer.
static void handle_message(int fd, const struct sockaddr_in *peer, const Message *msg) {
    char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &peer->sin_addr, ip, sizeof(ip));
    if (strcmp(msg->type, "SS_REGISTER") == 0) {
        // Step 3: Parse SS registration payload and index files
        // Payload format: "host=IP,client_port=PORT,storage=DIR,files=file1.txt,file2.txt,..."
        
        // Extract SS information from payload
        char ss_host[64] = {0};
        int ss_client_port = 0;
        char *host_start = strstr(msg->payload, "host=");
        char *port_start = strstr(msg->payload, "client_port=");
        
        if (host_start) {
            char *host_end = strchr(host_start + 5, ',');
            if (host_end) {
                size_t host_len = host_end - (host_start + 5);
                if (host_len < sizeof(ss_host)) {
                    memcpy(ss_host, host_start + 5, host_len);
                    ss_host[host_len] = '\0';
                }
            }
        }
        
        if (port_start) {
            ss_client_port = atoi(port_start + 12);
        }
        
        // Extract and parse file list
        char *files_start = strstr(msg->payload, "files=");
        int file_count = 0;
        
        if (files_start) {
            files_start += 6;  // Skip "files="
            if (*files_start != '\0') {
                // Parse comma-separated file list and index each file
                char *file_list = strdup(files_start);  // Make copy for parsing
                if (file_list) {
                    char *saveptr = NULL;
                    char *filename = strtok_r(file_list, ",", &saveptr);
                    
                    while (filename) {
                        // Index this file with SS username as placeholder owner
                        // Note: Real owner is stored in metadata on SS, but we don't load it during registration
                        // The owner will be updated when:
                        //   1. User creates the file (CREATE command updates owner)
                        //   2. Future enhancement: Load metadata from SS during registration
                        // For now, files indexed during SS registration show owner=ss1 until accessed
                        FileEntry *entry = index_add_file(filename, msg->username, ss_host, 
                                                          ss_client_port, msg->username);
                        if (entry) {
                            file_count++;
                            log_info("nm_file_indexed", "file=%s ss=%s owner=%s", 
                                     filename, msg->username, entry->owner);
                        }
                        filename = strtok_r(NULL, ",", &saveptr);
                    }
                    
                    free(file_list);
                }
            }
        }
        
        registry_add("SS", msg->username, msg->payload);
        log_info("nm_ss_register", "ip=%s user=%s files=%d indexed", ip, msg->username, file_count);
        
        Message ack = {0};
        (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
        (void)snprintf(ack.id, sizeof(ack.id), "%s", msg->id);
        (void)snprintf(ack.username, sizeof(ack.username), "%s", msg->username);
        (void)snprintf(ack.role, sizeof(ack.role), "%s", "NM");
        (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "registered");
        char line[MAX_LINE]; proto_format_line(&ack, line, sizeof(line));
        send_all(fd, line, strlen(line));
        return;
    }
    if (strcmp(msg->type, "CLIENT_REGISTER") == 0) {
        registry_add("CLIENT", msg->username, msg->payload);
        log_info("nm_client_register", "ip=%s user=%s", ip, msg->username);
        Message ack = {0};
        (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
        (void)snprintf(ack.id, sizeof(ack.id), "%s", msg->id);
        (void)snprintf(ack.username, sizeof(ack.username), "%s", msg->username);
        (void)snprintf(ack.role, sizeof(ack.role), "%s", "NM");
        (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "registered");
        char line[MAX_LINE]; proto_format_line(&ack, line, sizeof(line));
        send_all(fd, line, strlen(line));
        return;
    }
    if (strcmp(msg->type, "HEARTBEAT") == 0) {
        log_info("nm_heartbeat", "user=%s", msg->username);
        Message ack = {0};
        (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
        (void)snprintf(ack.id, sizeof(ack.id), "%s", msg->id);
        (void)snprintf(ack.username, sizeof(ack.username), "%s", msg->username);
        (void)snprintf(ack.role, sizeof(ack.role), "%s", "NM");
        (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "pong");
        char line[MAX_LINE]; proto_format_line(&ack, line, sizeof(line));
        send_all(fd, line, strlen(line));
        return;
    }
    
    // Step 6: Handle client commands
    // Parse payload: flags=FLAGS|arg1|arg2|...
    if (strcmp(msg->type, "VIEW") == 0) {
        // Extract flags from payload
        char flags[16] = {0};
        char *flags_start = strstr(msg->payload, "flags=");
        if (flags_start) {
            char *flags_end = strchr(flags_start + 6, '|');
            if (flags_end) {
                size_t flags_len = flags_end - (flags_start + 6);
                if (flags_len < sizeof(flags)) {
                    memcpy(flags, flags_start + 6, flags_len);
                    flags[flags_len] = '\0';
                }
            } else {
                strncpy(flags, flags_start + 6, sizeof(flags) - 1);
            }
        }
        
        log_info("nm_cmd_view", "user=%s flags=%s", msg->username, flags);
        handle_view(fd, msg->username, flags);
        return;
    }
    
    if (strcmp(msg->type, "CREATE") == 0) {
        // Filename is directly in payload
        char filename[256] = {0};
        size_t payload_len = strlen(msg->payload);
        size_t copy_len = (payload_len < sizeof(filename) - 1) ? payload_len : sizeof(filename) - 1;
        memcpy(filename, msg->payload, copy_len);
        filename[copy_len] = '\0';
        
        log_info("nm_cmd_create", "user=%s file=%s", msg->username, filename);
        handle_create(fd, msg->username, filename);
        return;
    }
    
    if (strcmp(msg->type, "DELETE") == 0) {
        // Filename is directly in payload
        char filename[256] = {0};
        size_t payload_len = strlen(msg->payload);
        size_t copy_len = (payload_len < sizeof(filename) - 1) ? payload_len : sizeof(filename) - 1;
        memcpy(filename, msg->payload, copy_len);
        filename[copy_len] = '\0';
        
        log_info("nm_cmd_delete", "user=%s file=%s", msg->username, filename);
        handle_delete(fd, msg->username, filename);
        return;
    }
    
    if (strcmp(msg->type, "INFO") == 0) {
        // Filename is directly in payload
        char filename[256] = {0};
        size_t payload_len = strlen(msg->payload);
        size_t copy_len = (payload_len < sizeof(filename) - 1) ? payload_len : sizeof(filename) - 1;
        memcpy(filename, msg->payload, copy_len);
        filename[copy_len] = '\0';
        
        log_info("nm_cmd_info", "user=%s file=%s", msg->username, filename);
        handle_info(fd, msg->username, filename);
        return;
    }
    
    if (strcmp(msg->type, "LIST") == 0) {
        log_info("nm_cmd_list", "user=%s", msg->username);
        handle_list(fd, msg->username);
        return;
    }
    
    // Unknown command
    log_error("nm_unknown_msg", "type=%s", msg->type);
    Error err = error_create(ERR_INVALID, "Unknown command: %s", msg->type);
    (void)send_error_response(fd, msg->id, msg->username, &err);
}

// Thread function: read lines, parse, and handle messages until peer closes.
static void *client_thread(void *arg) {
    ClientConnArg *c = (ClientConnArg*)arg;
    char line[MAX_LINE];
    while (g_running) {
        int n = recv_line(c->fd, line, sizeof(line));
        if (n <= 0) break;
        Message msg;
        if (proto_parse_line(line, &msg) == 0) {
            handle_message(c->fd, &c->addr, &msg);
        }
    }
    close(c->fd);
    free(c);
    return NULL;
}

static void on_sigint(int sig) { (void)sig; g_running = 0; }

int main(int argc, char **argv) {
    const char *host = "0.0.0.0"; int port = 5000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host") && i+1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i+1 < argc) port = atoi(argv[++i]);
    }
    
    // Step 3: Initialize file index and LRU cache
    index_init();
    log_info("nm_index_init", "File index initialized");
    
    signal(SIGINT, on_sigint);
    int server_fd = create_server_socket(host, port);
    if (server_fd < 0) { perror("NM listen"); return 1; }
    log_info("nm_listen", "host=%s port=%d", host, port);

    while (g_running) {
        struct sockaddr_in addr; socklen_t alen = sizeof(addr);
        int fd = accept(server_fd, (struct sockaddr*)&addr, &alen);
        if (fd < 0) { if (!g_running) break; continue; }
        ClientConnArg *c = (ClientConnArg*)calloc(1, sizeof(ClientConnArg));
        c->fd = fd; c->addr = addr;
        pthread_t th; (void)pthread_create(&th, NULL, client_thread, c); pthread_detach(th);
    }
    close(server_fd);
    return 0;
}


