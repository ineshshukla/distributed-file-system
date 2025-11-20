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
#include "write_session.h"
#include "runtime_state.h"

#define DEFAULT_WORKERS 8
#define WORK_QUEUE_CAP 64

typedef struct {
    int fds[WORK_QUEUE_CAP];
    int head;
    int tail;
    int count;
    pthread_mutex_t mu;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} WorkQueue;

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
    int worker_count;
    pthread_t workers[DEFAULT_WORKERS];
    WorkQueue queue;
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

static void encode_newlines(const char *src, char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) return;
    size_t pos = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (*src && pos < dst_len - 1) {
        if (*src == '\n') {
            dst[pos++] = '\x01';
        } else {
            dst[pos++] = *src;
        }
        src++;
    }
    dst[pos] = '\0';
}

static void work_queue_init(WorkQueue *q) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

static void work_queue_destroy(WorkQueue *q) {
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

static int work_queue_push(Ctx *ctx, int fd) {
    WorkQueue *q = &ctx->queue;
    pthread_mutex_lock(&q->mu);
    while (q->count >= WORK_QUEUE_CAP && ctx->running) {
        pthread_cond_wait(&q->not_full, &q->mu);
    }
    if (!ctx->running) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }
    q->fds[q->tail] = fd;
    q->tail = (q->tail + 1) % WORK_QUEUE_CAP;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mu);
    return 0;
}

static int work_queue_pop(Ctx *ctx) {
    WorkQueue *q = &ctx->queue;
    pthread_mutex_lock(&q->mu);
    while (q->count == 0) {
        if (!ctx->running) {
            pthread_mutex_unlock(&q->mu);
            return -1;
        }
        pthread_cond_wait(&q->not_empty, &q->mu);
    }
    int fd = q->fds[q->head];
    q->head = (q->head + 1) % WORK_QUEUE_CAP;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mu);
    return fd;
}

static void handle_command(Ctx *ctx, int client_fd, Message cmd_msg);

static void process_connection(Ctx *ctx, int client_fd) {
    char cmd_line[MAX_LINE];
    int n = recv_line(client_fd, cmd_line, sizeof(cmd_line));
    if (n <= 0) {
        close(client_fd);
        return;
    }
    Message cmd_msg;
    if (proto_parse_line(cmd_line, &cmd_msg) != 0) {
        log_error("ss_parse_error", "failed to parse command");
        close(client_fd);
        return;
    }
    handle_command(ctx, client_fd, cmd_msg);
}

static void *worker_thread(void *arg) {
    Ctx *ctx = (Ctx*)arg;
    while (ctx->running) {
        int fd = work_queue_pop(ctx);
        if (fd < 0) break;
        process_connection(ctx, fd);
    }
    return NULL;
}

static void *cmd_thread(void *arg) {
    Ctx *ctx = (Ctx*)arg;
    work_queue_init(&ctx->queue);
    ctx->worker_count = DEFAULT_WORKERS;
    for (int i = 0; i < ctx->worker_count; i++) {
        pthread_create(&ctx->workers[i], NULL, worker_thread, ctx);
    }

    int server_fd = create_server_socket(ctx->host, ctx->client_port);
    if (server_fd < 0) {
        log_error("ss_server_socket", "failed to create server socket on %s:%d", ctx->host, ctx->client_port);
        ctx->running = 0;
        pthread_cond_broadcast(&ctx->queue.not_empty);
        pthread_cond_broadcast(&ctx->queue.not_full);
        work_queue_destroy(&ctx->queue);
        return NULL;
    }
    ctx->server_fd = server_fd;
    log_info("ss_listen", "listening on %s:%d for commands", ctx->host, ctx->client_port);

    while (ctx->running) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&addr, &addr_len);
        if (client_fd < 0) {
            if (ctx->running) continue;
            break;
        }
        if (work_queue_push(ctx, client_fd) != 0) {
            close(client_fd);
            break;
        }
    }

    close(server_fd);
    pthread_mutex_lock(&ctx->queue.mu);
    ctx->running = 0;
    pthread_cond_broadcast(&ctx->queue.not_empty);
    pthread_cond_broadcast(&ctx->queue.not_full);
    pthread_mutex_unlock(&ctx->queue.mu);
    for (int i = 0; i < ctx->worker_count; i++) {
        pthread_join(ctx->workers[i], NULL);
    }
    work_queue_destroy(&ctx->queue);
    return NULL;
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

// Command handler logic for a single connection
static void handle_command(Ctx *ctx, int client_fd, Message cmd_msg) {
        
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
            close(client_fd);
            return;
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
        // Handle CREATE_FOLDER command
        else if (strcmp(cmd_msg.type, "CREATE_FOLDER") == 0) {
            const char *folder_path = cmd_msg.payload;
            
            log_info("ss_cmd_create_folder", "folder=%s", folder_path);
            
            // Create folder
            if (folder_create(ctx->storage_dir, folder_path) == 0) {
                // Send ACK
                Message ack = {0};
                (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
                (void)snprintf(ack.id, sizeof(ack.id), "%s", cmd_msg.id);
                (void)snprintf(ack.username, sizeof(ack.username), "%s", cmd_msg.username);
                (void)snprintf(ack.role, sizeof(ack.role), "%s", "SS");
                (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "folder_created");
                
                char ack_line[MAX_LINE];
                proto_format_line(&ack, ack_line, sizeof(ack_line));
                send_all(client_fd, ack_line, strlen(ack_line));
                
                log_info("ss_folder_created", "folder=%s", folder_path);
            } else {
                // Send error
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                  "INTERNAL", "Failed to create folder",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                log_error("ss_create_folder_failed", "folder=%s", folder_path);
            }
            close(client_fd);
        }
        // Handle MOVE command
        else if (strcmp(cmd_msg.type, "MOVE") == 0) {
            // Payload format: "filename|old_folder_path|new_folder_path"
            char filename[256] = {0};
            char old_folder[512] = {0};
            char new_folder[512] = {0};
            
            // Parse payload
            const char *p1 = strchr(cmd_msg.payload, '|');
            if (p1) {
                size_t fname_len = p1 - cmd_msg.payload;
                if (fname_len < sizeof(filename)) {
                    memcpy(filename, cmd_msg.payload, fname_len);
                    filename[fname_len] = '\0';
                }
                
                const char *p2 = strchr(p1 + 1, '|');
                if (p2) {
                    size_t old_len = p2 - (p1 + 1);
                    if (old_len < sizeof(old_folder)) {
                        memcpy(old_folder, p1 + 1, old_len);
                        old_folder[old_len] = '\0';
                    }
                    
                    size_t new_len = strlen(p2 + 1);
                    if (new_len < sizeof(new_folder)) {
                        strcpy(new_folder, p2 + 1);
                    }
                }
            }
            
            log_info("ss_cmd_move", "file=%s from=%s to=%s", filename, old_folder, new_folder);
            
            // Move file
            if (file_move(ctx->storage_dir, filename, old_folder, new_folder) == 0) {
                // Send ACK
                Message ack = {0};
                (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
                (void)snprintf(ack.id, sizeof(ack.id), "%s", cmd_msg.id);
                (void)snprintf(ack.username, sizeof(ack.username), "%s", cmd_msg.username);
                (void)snprintf(ack.role, sizeof(ack.role), "%s", "SS");
                (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "file_moved");
                
                char ack_line[MAX_LINE];
                proto_format_line(&ack, ack_line, sizeof(ack_line));
                send_all(client_fd, ack_line, strlen(ack_line));
                
                log_info("ss_file_moved", "file=%s from=%s to=%s", filename, old_folder, new_folder);
            } else {
                // Send error
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                  "NOT_FOUND", "File not found or move failed",
                                  error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                log_error("ss_move_failed", "file=%s", filename);
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
                return;
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
                return;
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
                return;
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
                return;
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
            return;
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
                return;
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
                return;
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
                return;
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
                return;
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
            return;
        }
        else if (strcmp(cmd_msg.type, "GET_FILE") == 0) {
            const char *filename = cmd_msg.payload;

            char content[65536];
            size_t actual_size = 0;
            if (file_read(ctx->storage_dir, filename, content, sizeof(content), &actual_size) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                   "NOT_FOUND", "File not found",
                                   error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                return;
            }

            size_t content_pos = 0;
            while (content_pos < actual_size) {
                Message data_msg = {0};
                (void)snprintf(data_msg.type, sizeof(data_msg.type), "%s", "DATA");
                (void)snprintf(data_msg.id, sizeof(data_msg.id), "%s", cmd_msg.id);
                (void)snprintf(data_msg.username, sizeof(data_msg.username), "%s", cmd_msg.username);
                (void)snprintf(data_msg.role, sizeof(data_msg.role), "%s", "SS");

                size_t payload_pos = 0;
                size_t payload_max = sizeof(data_msg.payload) - 1;
                while (content_pos < actual_size && payload_pos < payload_max) {
                    char c = content[content_pos++];
                    data_msg.payload[payload_pos++] = (c == '\n') ? '\x01' : c;
                }
                data_msg.payload[payload_pos] = '\0';

                char data_buf[MAX_LINE];
                if (proto_format_line(&data_msg, data_buf, sizeof(data_buf)) == 0) {
                    send_all(client_fd, data_buf, strlen(data_buf));
                }
            }

            Message stop_msg = {0};
            (void)snprintf(stop_msg.type, sizeof(stop_msg.type), "%s", "STOP");
            (void)snprintf(stop_msg.id, sizeof(stop_msg.id), "%s", cmd_msg.id);
            (void)snprintf(stop_msg.username, sizeof(stop_msg.username), "%s", cmd_msg.username);
            (void)snprintf(stop_msg.role, sizeof(stop_msg.role), "%s", "SS");
            stop_msg.payload[0] = '\0';

            char stop_buf[MAX_LINE];
            if (proto_format_line(&stop_msg, stop_buf, sizeof(stop_buf)) == 0) {
                send_all(client_fd, stop_buf, strlen(stop_buf));
            }
            close(client_fd);
            return;
        }
        // Handle WRITE command
        else if (strcmp(cmd_msg.type, "WRITE") == 0) {
            char filename[256] = {0};
            int sentence_index = 0;
            if (strlen(cmd_msg.payload) > 0) {
                char payload_copy[512];
                size_t payload_len = strlen(cmd_msg.payload);
                size_t copy_len = (payload_len < sizeof(payload_copy) - 1) ? payload_len : sizeof(payload_copy) - 1;
                memcpy(payload_copy, cmd_msg.payload, copy_len);
                payload_copy[copy_len] = '\0';
                char *sep = strchr(payload_copy, '|');
                if (sep) {
                    *sep = '\0';
                    sentence_index = atoi(sep + 1);
                }
                size_t fn_len = strlen(payload_copy);
                size_t fn_copy = (fn_len < sizeof(filename) - 1) ? fn_len : sizeof(filename) - 1;
                memcpy(filename, payload_copy, fn_copy);
                filename[fn_copy] = '\0';
            } else {
                size_t fn_len = strlen(cmd_msg.payload);
                size_t fn_copy = (fn_len < sizeof(filename) - 1) ? fn_len : sizeof(filename) - 1;
                memcpy(filename, cmd_msg.payload, fn_copy);
                filename[fn_copy] = '\0';
            }

            FileMetadata meta;
            if (metadata_load(ctx->storage_dir, filename, &meta) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                   "NOT_FOUND", "File not found",
                                   error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                return;
            }
            if (!acl_check_write(&meta.acl, cmd_msg.username)) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                   "UNAUTHORIZED", "User does not have write access",
                                   error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                return;
            }

            WriteSession session;
            char err_buf[256];
            char *current_sentence = NULL;
            if (write_session_begin(&session, ctx->storage_dir, filename,
                                    sentence_index, cmd_msg.username,
                                    &current_sentence, err_buf, sizeof(err_buf)) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                   "INVALID", err_buf,
                                   error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                return;
            }

            Message ready = {0};
            (void)snprintf(ready.type, sizeof(ready.type), "%s", "WRITE_READY");
            (void)snprintf(ready.id, sizeof(ready.id), "%s", cmd_msg.id);
            (void)snprintf(ready.username, sizeof(ready.username), "%s", cmd_msg.username);
            (void)snprintf(ready.role, sizeof(ready.role), "%s", "SS");
            if (current_sentence && strlen(current_sentence) > 0) {
                encode_newlines(current_sentence, ready.payload, sizeof(ready.payload));
            } else {
                ready.payload[0] = '\0';
            }
            char ready_buf[MAX_LINE];
            proto_format_line(&ready, ready_buf, sizeof(ready_buf));
            send_all(client_fd, ready_buf, strlen(ready_buf));
            free(current_sentence);

            int write_active = 1;
            while (write_active && ctx->running) {
                char write_line[MAX_LINE];
                int rn = recv_line(client_fd, write_line, sizeof(write_line));
                if (rn <= 0) {
                    log_error("ss_write_disconnect", "user=%s file=%s", cmd_msg.username, filename);
                    write_session_abort(&session);
                    break;
                }
                Message write_cmd;
                if (proto_parse_line(write_line, &write_cmd) != 0) {
                    log_error("ss_write_parse", "invalid write message");
                    continue;
                }
                if (strcmp(write_cmd.type, "WRITE_EDIT") == 0) {
                    char payload_copy[512];
                    strncpy(payload_copy, write_cmd.payload, sizeof(payload_copy) - 1);
                    payload_copy[sizeof(payload_copy) - 1] = '\0';
                    char *sep = strchr(payload_copy, '|');
                    if (!sep) {
                    char error_buf[MAX_LINE];
                    proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                       "INVALID", "Invalid write payload",
                                       error_buf, sizeof(error_buf));
                    send_all(client_fd, error_buf, strlen(error_buf));
                    continue;
                    }
                    *sep = '\0';
                    int word_index = atoi(payload_copy);
                    const char *content = sep + 1;
                    if (write_session_apply_edit(&session, word_index, content,
                                                 err_buf, sizeof(err_buf)) != 0) {
                        char error_buf[MAX_LINE];
                        proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                           "INVALID", err_buf,
                                           error_buf, sizeof(error_buf));
                        send_all(client_fd, error_buf, strlen(error_buf));
                        continue;
                    }
                    Message ack = {0};
                    (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
                    (void)snprintf(ack.id, sizeof(ack.id), "%s", cmd_msg.id);
                    (void)snprintf(ack.username, sizeof(ack.username), "%s", cmd_msg.username);
                    (void)snprintf(ack.role, sizeof(ack.role), "%s", "SS");
                    (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "edit applied");
                    char ack_buf[MAX_LINE];
                    proto_format_line(&ack, ack_buf, sizeof(ack_buf));
                    send_all(client_fd, ack_buf, strlen(ack_buf));
                } else if (strcmp(write_cmd.type, "WRITE_DONE") == 0) {
                    if (write_session_commit(&session, err_buf, sizeof(err_buf)) != 0) {
                        char error_buf[MAX_LINE];
                        proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                           "INVALID", err_buf,
                                           error_buf, sizeof(error_buf));
                        send_all(client_fd, error_buf, strlen(error_buf));
                        write_session_abort(&session);
                    } else {
                        Message ack = {0};
                        (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
                        (void)snprintf(ack.id, sizeof(ack.id), "%s", cmd_msg.id);
                        (void)snprintf(ack.username, sizeof(ack.username), "%s", cmd_msg.username);
                        (void)snprintf(ack.role, sizeof(ack.role), "%s", "SS");
                        (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "Write Successful!");
                        char ack_buf[MAX_LINE];
                        proto_format_line(&ack, ack_buf, sizeof(ack_buf));
                        send_all(client_fd, ack_buf, strlen(ack_buf));
                    }
                    write_active = 0;
                } else if (strcmp(write_cmd.type, "WRITE_ABORT") == 0) {
                    write_session_abort(&session);
                    Message ack = {0};
                    (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
                    (void)snprintf(ack.id, sizeof(ack.id), "%s", cmd_msg.id);
                    (void)snprintf(ack.username, sizeof(ack.username), "%s", cmd_msg.username);
                    (void)snprintf(ack.role, sizeof(ack.role), "%s", "SS");
                    (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "Write aborted");
                    char ack_buf[MAX_LINE];
                    proto_format_line(&ack, ack_buf, sizeof(ack_buf));
                    send_all(client_fd, ack_buf, strlen(ack_buf));
                    write_active = 0;
                } else {
                    char error_buf[MAX_LINE];
                    proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                       "INVALID", "Unknown write command",
                                       error_buf, sizeof(error_buf));
                    send_all(client_fd, error_buf, strlen(error_buf));
                }
            }
            close(client_fd);
            return;
        }
        else if (strcmp(cmd_msg.type, "UNDO") == 0) {
            const char *filename = cmd_msg.payload;
            const char *username = cmd_msg.username;

            log_info("ss_cmd_undo", "file=%s user=%s", filename, username);

            if (!undo_exists(ctx->storage_dir, filename)) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                   "NO_UNDO", "No undo information available",
                                   error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                return;
            }

            FileMetadata current_meta;
            if (metadata_load(ctx->storage_dir, filename, &current_meta) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                   "NOT_FOUND", "File not found",
                                   error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                return;
            }

            if (undo_restore_state(ctx->storage_dir, filename) != 0) {
                char error_buf[MAX_LINE];
                proto_format_error(cmd_msg.id, cmd_msg.username, "SS",
                                   "INTERNAL", "Failed to restore undo state",
                                   error_buf, sizeof(error_buf));
                send_all(client_fd, error_buf, strlen(error_buf));
                close(client_fd);
                return;
            }

            FileMetadata restored_meta;
            if (metadata_load(ctx->storage_dir, filename, &restored_meta) == 0) {
                restored_meta.acl = current_meta.acl;
                metadata_save(ctx->storage_dir, filename, &restored_meta);
            }

            Message ack = {0};
            (void)snprintf(ack.type, sizeof(ack.type), "%s", "ACK");
            (void)snprintf(ack.id, sizeof(ack.id), "%s", cmd_msg.id);
            (void)snprintf(ack.username, sizeof(ack.username), "%s", cmd_msg.username);
            (void)snprintf(ack.role, sizeof(ack.role), "%s", "SS");
            (void)snprintf(ack.payload, sizeof(ack.payload), "%s", "Undo Successful!");

            char ack_line[MAX_LINE];
            proto_format_line(&ack, ack_line, sizeof(ack_line));
            send_all(client_fd, ack_line, strlen(ack_line));
            log_info("ss_undo_restored", "file=%s", filename);
            close(client_fd);
            return;
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
                return;
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
                return;
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
            return;
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
                return;
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
                return;
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
            return;
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
            close(client_fd);
            return;
        }
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
    if (ctx.username) {
        char log_path[128];
        snprintf(log_path, sizeof(log_path), "ss_%s.log", ctx.username);
        log_set_file(log_path);
    }
    // Ensure storage directory exists
    ensure_storage_dir(ctx.storage_dir);
    
    // Phase 2: Scan directory for existing files
    // This discovers all files that were created before SS restart
    // The file list will be sent to NM during registration
    log_info("ss_scan_start", "scanning storage directory: %s", ctx.storage_dir);
    ScanResult scan_result = scan_directory(ctx.storage_dir, "files");
    log_info("ss_scan_complete", "found %d files", scan_result.count);

    runtime_state_init();
    
    // Build file list string for registration payload
    // Format: "host=IP,client_port=PORT,storage=DIR,files=file1.txt,file2.txt,..."
    char file_list[4096];
    if (build_file_list_string(&scan_result, ctx.storage_dir, file_list, sizeof(file_list)) != 0) {
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
    runtime_state_shutdown();
    return 0;
}


