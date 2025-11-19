// Storage Server (SS): prepares local storage dir, registers to NM,
// and sends periodic heartbeats.
// Phase 2: Now includes file scanning and storage management.
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
            close(client_fd);
        }
        // Handle READ command (from client)
        else if (strcmp(cmd_msg.type, "READ") == 0) {
            const char *filename = cmd_msg.payload;
            const char *username = cmd_msg.username;
            
            log_info("ss_cmd_read", "file=%s user=%s", filename, username);
            
            // Check if file exists
            if (!file_exists(ctx->storage_dir, filename)) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, username, "SS",
                                  "NOT_FOUND", "File not found",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_read_failed", "file=%s reason=not_found", filename);
                continue;
            }
            
            // Load metadata to check read access
            FileMetadata meta;
            if (metadata_load(ctx->storage_dir, filename, &meta) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, username, "SS",
                                  "INTERNAL", "Failed to load file metadata",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_read_failed", "file=%s reason=metadata_load_failed", filename);
                continue;
            }
            
            // Check read access using ACL
            if (!acl_check_read(&meta.acl, username)) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, username, "SS",
                                  "UNAUTHORIZED", "User does not have read access",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_read_failed", "file=%s user=%s reason=unauthorized", filename, username);
                continue;
            }
            
            // Read file content
            char content[65536];  // Max 64KB for now
            size_t actual_size = 0;
            if (file_read(ctx->storage_dir, filename, content, sizeof(content), &actual_size) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, username, "SS",
                                  "INTERNAL", "Failed to read file content",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_read_failed", "file=%s reason=read_failed", filename);
                continue;
            }
            
            // Send file content
            // Replace newlines with \x01 (SOH) to avoid breaking line-based protocol
            // Client will convert back to \n
            // Send in chunks to handle large files
            size_t content_pos = 0;
            size_t content_len = actual_size;
            
            while (content_pos < content_len) {
                Message data_msg = {0};
                (void)snprintf(data_msg.type, sizeof(data_msg.type), "%s", "DATA");
                (void)snprintf(data_msg.id, sizeof(data_msg.id), "%s", cmd_msg.id);
                (void)snprintf(data_msg.username, sizeof(data_msg.username), "%s", username);
                (void)snprintf(data_msg.role, sizeof(data_msg.role), "%s", "SS");
                
                // Copy chunk, replacing \n with \x01
                size_t payload_pos = 0;
                size_t payload_max = sizeof(data_msg.payload) - 1;
                
                while (content_pos < content_len && payload_pos < payload_max) {
                    if (content[content_pos] == '\n') {
                        data_msg.payload[payload_pos++] = '\x01';
                    } else {
                        data_msg.payload[payload_pos++] = content[content_pos];
                    }
                    content_pos++;
                }
                data_msg.payload[payload_pos] = '\0';
                
                char data_buf[MAX_LINE];
                if (proto_format_line(&data_msg, data_buf, sizeof(data_buf)) == 0) {
                    send_all(client_fd, data_buf, strlen(data_buf));
                }
            }
            
            // Send STOP packet
            Message stop_msg = {0};
            (void)snprintf(stop_msg.type, sizeof(stop_msg.type), "%s", "STOP");
            (void)snprintf(stop_msg.id, sizeof(stop_msg.id), "%s", cmd_msg.id);
            (void)snprintf(stop_msg.username, sizeof(stop_msg.username), "%s", username);
            (void)snprintf(stop_msg.role, sizeof(stop_msg.role), "%s", "SS");
            stop_msg.payload[0] = '\0';
            
            char stop_buf[MAX_LINE];
            if (proto_format_line(&stop_msg, stop_buf, sizeof(stop_buf)) == 0) {
                send_all(client_fd, stop_buf, strlen(stop_buf));
            }
            
            // Update last accessed timestamp
            metadata_update_last_accessed(ctx->storage_dir, filename);
            
            log_info("ss_file_read", "file=%s user=%s size=%zu", filename, username, actual_size);
            close(client_fd);
        }
        // Handle STREAM command (from client)
        else if (strcmp(cmd_msg.type, "STREAM") == 0) {
            const char *filename = cmd_msg.payload;
            const char *username = cmd_msg.username;
            
            log_info("ss_cmd_stream", "file=%s user=%s", filename, username);
            
            // Check if file exists
            if (!file_exists(ctx->storage_dir, filename)) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, username, "SS",
                                  "NOT_FOUND", "File not found",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_stream_failed", "file=%s reason=not_found", filename);
                continue;
            }
            
            // Load metadata to check read access
            FileMetadata meta;
            if (metadata_load(ctx->storage_dir, filename, &meta) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, username, "SS",
                                  "INTERNAL", "Failed to load file metadata",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_stream_failed", "file=%s reason=metadata_load_failed", filename);
                continue;
            }
            
            // Check read access using ACL
            if (!acl_check_read(&meta.acl, username)) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, username, "SS",
                                  "UNAUTHORIZED", "User does not have read access",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_stream_failed", "file=%s user=%s reason=unauthorized", filename, username);
                continue;
            }
            
            // Read file content
            char content[65536];  // Max 64KB for now
            size_t actual_size = 0;
            if (file_read(ctx->storage_dir, filename, content, sizeof(content), &actual_size) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, username, "SS",
                                  "INTERNAL", "Failed to read file content",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_stream_failed", "file=%s reason=read_failed", filename);
                continue;
            }
            
            // Parse content into words (split by spaces)
            // Send each word with 0.1s delay
            char *word_start = content;
            char *word_end;
            int word_count = 0;
            
            // Skip leading whitespace
            while (*word_start == ' ' || *word_start == '\t' || *word_start == '\n' || *word_start == '\r') {
                word_start++;
            }
            
            while (*word_start != '\0') {
                // Find end of word (space, tab, newline, or null)
                word_end = word_start;
                while (*word_end != '\0' && *word_end != ' ' && *word_end != '\t' && 
                       *word_end != '\n' && *word_end != '\r') {
                    word_end++;
                }
                
                if (word_end > word_start) {
                    // Extract word
                    size_t word_len = word_end - word_start;
                    char word[256] = {0};
                    size_t copy_len = (word_len < sizeof(word) - 1) ? word_len : sizeof(word) - 1;
                    memcpy(word, word_start, copy_len);
                    word[copy_len] = '\0';
                    
                    // Send word as DATA packet
                    Message data_msg = {0};
                    (void)snprintf(data_msg.type, sizeof(data_msg.type), "%s", "DATA");
                    (void)snprintf(data_msg.id, sizeof(data_msg.id), "%s", cmd_msg.id);
                    (void)snprintf(data_msg.username, sizeof(data_msg.username), "%s", username);
                    (void)snprintf(data_msg.role, sizeof(data_msg.role), "%s", "SS");
                    (void)snprintf(data_msg.payload, sizeof(data_msg.payload), "%s", word);
                    
                    char data_buf[MAX_LINE];
                    if (proto_format_line(&data_msg, data_buf, sizeof(data_buf)) == 0) {
                        send_all(client_fd, data_buf, strlen(data_buf));
                    }
                    
                    word_count++;
                    
                    // Delay 0.1 seconds using nanosleep
                    struct timespec delay = {0, 100000000};  // 0.1 seconds = 100000000 nanoseconds
                    nanosleep(&delay, NULL);
                }
                
                // Move to next word (skip whitespace)
                word_start = word_end;
                while (*word_start == ' ' || *word_start == '\t' || *word_start == '\n' || *word_start == '\r') {
                    word_start++;
                }
            }
            
            // Send STOP packet
            Message stop_msg = {0};
            (void)snprintf(stop_msg.type, sizeof(stop_msg.type), "%s", "STOP");
            (void)snprintf(stop_msg.id, sizeof(stop_msg.id), "%s", cmd_msg.id);
            (void)snprintf(stop_msg.username, sizeof(stop_msg.username), "%s", username);
            (void)snprintf(stop_msg.role, sizeof(stop_msg.role), "%s", "SS");
            stop_msg.payload[0] = '\0';
            
            char stop_buf[MAX_LINE];
            if (proto_format_line(&stop_msg, stop_buf, sizeof(stop_buf)) == 0) {
                send_all(client_fd, stop_buf, strlen(stop_buf));
            }
            
            // Update last accessed timestamp
            metadata_update_last_accessed(ctx->storage_dir, filename);
            
            log_info("ss_file_streamed", "file=%s user=%s words=%d", filename, username, word_count);
            close(client_fd);
        }
        // Handle UPDATE_ACL command (from NM)
        else if (strcmp(cmd_msg.type, "UPDATE_ACL") == 0) {
            // Payload format: "action=ADD|REMOVE,flag=R|W,filename=FILE,target_user=USER"
            const char *payload = cmd_msg.payload;
            
            // Parse payload
            char action[16] = {0};  // ADD or REMOVE
            char flag[16] = {0};    // R or W
            char filename[256] = {0};
            char target_user[64] = {0};
            
            // Parse: action=ADD,flag=R,filename=test.txt,target_user=bob
            char *action_start = strstr(payload, "action=");
            char *flag_start = strstr(payload, "flag=");
            char *file_start = strstr(payload, "filename=");
            char *user_start = strstr(payload, "target_user=");
            
            if (action_start) {
                char *action_end = strchr(action_start + 7, ',');
                if (action_end) {
                    size_t action_len = action_end - (action_start + 7);
                    if (action_len < sizeof(action)) {
                        memcpy(action, action_start + 7, action_len);
                        action[action_len] = '\0';
                    }
                } else {
                    size_t action_len = strlen(action_start + 7);
                    if (action_len < sizeof(action)) {
                        memcpy(action, action_start + 7, action_len);
                        action[action_len] = '\0';
                    }
                }
            }
            
            if (flag_start) {
                char *flag_end = strchr(flag_start + 5, ',');
                if (flag_end) {
                    size_t flag_len = flag_end - (flag_start + 5);
                    if (flag_len < sizeof(flag)) {
                        memcpy(flag, flag_start + 5, flag_len);
                        flag[flag_len] = '\0';
                    }
                } else {
                    size_t flag_len = strlen(flag_start + 5);
                    if (flag_len < sizeof(flag)) {
                        memcpy(flag, flag_start + 5, flag_len);
                        flag[flag_len] = '\0';
                    }
                }
            }
            
            if (file_start) {
                char *file_end = strchr(file_start + 9, ',');
                if (file_end) {
                    size_t file_len = file_end - (file_start + 9);
                    if (file_len < sizeof(filename)) {
                        memcpy(filename, file_start + 9, file_len);
                        filename[file_len] = '\0';
                    }
                } else {
                    size_t file_len = strlen(file_start + 9);
                    if (file_len < sizeof(filename)) {
                        memcpy(filename, file_start + 9, file_len);
                        filename[file_len] = '\0';
                    }
                }
            }
            
            if (user_start) {
                size_t user_len = strlen(user_start + 12);
                if (user_len < sizeof(target_user)) {
                    memcpy(target_user, user_start + 12, user_len);
                    target_user[user_len] = '\0';
                }
            }
            
            log_info("ss_cmd_update_acl", "file=%s action=%s flag=%s target=%s", 
                     filename, action, flag, target_user);
            
            // Load metadata
            FileMetadata meta;
            if (metadata_load(ctx->storage_dir, filename, &meta) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                  "NOT_FOUND", "File not found",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_update_acl_failed", "file=%s reason=not_found", filename);
                continue;
            }
            
            // Update ACL based on action
            if (strcmp(action, "ADD") == 0) {
                if (strcmp(flag, "R") == 0) {
                    acl_add_read(&meta.acl, target_user);
                } else if (strcmp(flag, "W") == 0) {
                    acl_add_write(&meta.acl, target_user);
                }
            } else if (strcmp(action, "REMOVE") == 0) {
                acl_remove(&meta.acl, target_user);
            }
            
            // Save updated metadata
            if (metadata_save(ctx->storage_dir, filename, &meta) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                  "INTERNAL", "Failed to save metadata",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_update_acl_failed", "file=%s reason=save_failed", filename);
                continue;
            }
            
            // Send ACK
            Message ack = {0};
            (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
            (void)snprintf(ack.id, sizeof(ack.id), "%s", cmd_msg.id);
            (void)snprintf(ack.username, sizeof(ack.username), "%s", cmd_msg.username);
            (void)snprintf(ack.role, sizeof(ack.role), "%s", "SS");
            (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "acl_updated");
            
            char ack_line[MAX_LINE];
            proto_format_line(&ack, ack_line, sizeof(ack_line));
            send_all(client_fd, ack_line, strlen(ack_line));
            
            log_info("ss_acl_updated", "file=%s action=%s target=%s", filename, action, target_user);
            close(client_fd);
        }
        // Handle GET_ACL command (from NM)
        else if (strcmp(cmd_msg.type, "GET_ACL") == 0) {
            const char *filename = cmd_msg.payload;

            log_info("ss_cmd_get_acl", "file=%s requester=%s", filename, cmd_msg.username);

            FileMetadata meta;
            if (metadata_load(ctx->storage_dir, filename, &meta) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                   "NOT_FOUND", "File not found",
                                   error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_get_acl_failed", "file=%s reason=not_found", filename);
                continue;
            }

            char acl_buf[4096];
            if (acl_serialize(&meta.acl, acl_buf, sizeof(acl_buf)) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                   "INTERNAL", "Failed to serialize ACL",
                                   error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                log_error("ss_get_acl_failed", "file=%s reason=serialize_failed", filename);
                continue;
            }

            // Replace newlines with \x01
            Message acl_msg = {0};
            (void)snprintf(acl_msg.type, sizeof(acl_msg.type), "%s", "ACL");
            (void)snprintf(acl_msg.id, sizeof(acl_msg.id), "%s", cmd_msg.id);
            (void)snprintf(acl_msg.username, sizeof(acl_msg.username), "%s", cmd_msg.username);
            (void)snprintf(acl_msg.role, sizeof(acl_msg.role), "%s", "SS");

            size_t payload_pos = 0;
            size_t payload_max = sizeof(acl_msg.payload) - 1;
            for (size_t i = 0; i < strlen(acl_buf) && payload_pos < payload_max; i++) {
                if (acl_buf[i] == '\n') {
                    acl_msg.payload[payload_pos++] = '\x01';
                } else {
                    acl_msg.payload[payload_pos++] = acl_buf[i];
                }
            }
            acl_msg.payload[payload_pos] = '\0';

            char acl_line[MAX_LINE];
            if (proto_format_line(&acl_msg, acl_line, sizeof(acl_line)) == 0) {
                send_all(client_fd, acl_line, strlen(acl_line));
            }

            log_info("ss_acl_sent", "file=%s requester=%s", filename, cmd_msg.username);
            close(client_fd);
        }
        // Unknown command
        else {
            log_error("ss_unknown_cmd", "type=%s", cmd_msg.type);
            char error_buf[MAX_LINE];
            proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                              "INVALID", "Unknown command",
                              error_buf, sizeof(error_buf));
            send_all(client_fd, error_buf, strlen(error_buf));
            close(client_fd);
        }
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


