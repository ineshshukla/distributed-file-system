#include "commands.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../common/net.h"
#include "../common/log.h"
#include "registry.h"

static int get_ss_connection_for_file(const FileEntry *entry);

// Helper: Fetch ACL from storage server
static int fetch_acl_from_ss(const FileEntry *entry, ACL *acl_out) {
    if (!entry || !acl_out) return -1;

    int ss_fd = get_ss_connection_for_file(entry);
    if (ss_fd < 0) {
        return -1;
    }

    // Send GET_ACL command
    Message req = {0};
    (void)snprintf(req.type, sizeof(req.type), "%s", "GET_ACL");
    (void)snprintf(req.id, sizeof(req.id), "%s", "1");
    (void)snprintf(req.username, sizeof(req.username), "%s", "NM");
    (void)snprintf(req.role, sizeof(req.role), "%s", "NM");
    (void)snprintf(req.payload, sizeof(req.payload), "%s", entry->filename);

    char req_buf[MAX_LINE];
    if (proto_format_line(&req, req_buf, sizeof(req_buf)) != 0) {
        close(ss_fd);
        return -1;
    }
    if (send_all(ss_fd, req_buf, strlen(req_buf)) != 0) {
        close(ss_fd);
        return -1;
    }

    // Receive response
    char resp_buf[MAX_LINE];
    int n = recv_line(ss_fd, resp_buf, sizeof(resp_buf));
    close(ss_fd);
    if (n <= 0) {
        return -1;
    }

    Message resp;
    if (proto_parse_line(resp_buf, &resp) != 0) {
        return -1;
    }

    if (strcmp(resp.type, "ERROR") == 0) {
        return -1;
    }

    if (strcmp(resp.type, "ACL") != 0) {
        return -1;
    }

    // Convert \x01 back to \n
    char acl_serialized[4096];
    size_t pos = 0;
    for (size_t i = 0; i < strlen(resp.payload) && pos < sizeof(acl_serialized) - 1; i++) {
        if (resp.payload[i] == '\x01') {
            acl_serialized[pos++] = '\n';
        } else {
            acl_serialized[pos++] = resp.payload[i];
        }
    }
    acl_serialized[pos] = '\0';

    if (acl_deserialize(acl_out, acl_serialized) != 0) {
        return -1;
    }

    return 0;
}
// Helper: Send error response
int send_error_response(int client_fd, const char *id, const char *username,
                       const Error *error) {
    if (!error) return -1;
    
    const char *error_code = error_code_to_string(error->code);
    const char *error_msg = error->message;
    
    char resp_buf[MAX_LINE];
    if (proto_format_error(id, username, "NM", error_code, error_msg,
                          resp_buf, sizeof(resp_buf)) != 0) {
        return -1;
    }
    
    return send_all(client_fd, resp_buf, strlen(resp_buf));
}

// Helper: Send success response
int send_success_response(int client_fd, const char *id, const char *username,
                          const char *message) {
    Message resp = {0};
    (void)snprintf(resp.type, sizeof(resp.type), "%s", "ACK");
    (void)snprintf(resp.id, sizeof(resp.id), "%s", id ? id : "");
    (void)snprintf(resp.username, sizeof(resp.username), "%s", username ? username : "");
    (void)snprintf(resp.role, sizeof(resp.role), "%s", "NM");
    (void)snprintf(resp.payload, sizeof(resp.payload), "%s", message ? message : "");
    
    char resp_buf[MAX_LINE];
    if (proto_format_line(&resp, resp_buf, sizeof(resp_buf)) != 0) {
        return -1;
    }
    
    return send_all(client_fd, resp_buf, strlen(resp_buf));
}

// Helper: Send data response
int send_data_response(int client_fd, const char *id, const char *username,
                      const char *data) {
    Message resp = {0};
    (void)snprintf(resp.type, sizeof(resp.type), "%s", "DATA");
    (void)snprintf(resp.id, sizeof(resp.id), "%s", id ? id : "");
    (void)snprintf(resp.username, sizeof(resp.username), "%s", username ? username : "");
    (void)snprintf(resp.role, sizeof(resp.role), "%s", "NM");
    
    // Copy data to payload, ensuring it fits (payload is 1792 bytes)
    // Replace newlines with \x01 (SOH - Start of Heading) to avoid breaking the line-based protocol
    // This character is unlikely to appear in normal text and won't be stripped by the parser
    // Client will convert \x01 back to \n
    if (data && strlen(data) > 0) {
        size_t data_len = strlen(data);
        size_t payload_pos = 0;
        size_t payload_max = sizeof(resp.payload) - 1;
        
        for (size_t i = 0; i < data_len && payload_pos < payload_max; i++) {
            if (data[i] == '\n') {
                // Replace newline with \x01 (SOH) as placeholder
                if (payload_pos + 1 < payload_max) {
                    resp.payload[payload_pos++] = '\x01';
                }
            } else {
                resp.payload[payload_pos++] = data[i];
            }
        }
        resp.payload[payload_pos] = '\0';
    } else {
        resp.payload[0] = '\0';
    }
    
    char resp_buf[MAX_LINE];
    if (proto_format_line(&resp, resp_buf, sizeof(resp_buf)) != 0) {
        log_error("nm_send_data_fmt", "failed to format response");
        return -1;
    }
    
    int result = send_all(client_fd, resp_buf, strlen(resp_buf));
    if (result != 0) {
        log_error("nm_send_data_send", "failed to send response");
    }
    return result;
}

// Helper: Get SS connection for a file
// Returns: SS connection fd, or -1 on error
static int get_ss_connection_for_file(const FileEntry *entry) {
    if (!entry) return -1;
    
    // Connect to SS using host and port from entry
    int fd = connect_to_host(entry->ss_host, entry->ss_client_port);
    if (fd < 0) {
        return -1;
    }
    
    return fd;
}

// Helper: Find SS by username (for CREATE - need to select SS)
// Returns: SS connection fd, or -1 on error
static int find_ss_connection(const char *ss_username) {
    if (!ss_username) return -1;
    
    // Get SS info from registry
    char ss_host[64] = {0};
    int ss_client_port = 0;
    
    if (registry_get_ss_info(ss_username, ss_host, sizeof(ss_host), &ss_client_port) != 0) {
        return -1;
    }
    
    // Connect to SS
    if (strlen(ss_host) > 0 && ss_client_port > 0) {
        return connect_to_host(ss_host, ss_client_port);
    }
    return -1;
}

// Helper: Load owner from SS metadata if not already set
// Returns: 0 on success, -1 on error
static int load_owner_from_ss(FileEntry *entry) {
    if (!entry) return -1;
    
    // If owner is already set (non-empty), no need to load
    if (entry->owner[0] != '\0') {
        return 0;  // Already has owner
    }
    
    // Connect to SS
    int ss_fd = get_ss_connection_for_file(entry);
    if (ss_fd < 0) {
        log_error("nm_load_owner", "Cannot connect to SS for file=%s", entry->filename);
        return -1;
    }
    
    // Send GETMETA command to SS to get metadata
    Message meta_cmd = {0};
    (void)snprintf(meta_cmd.type, sizeof(meta_cmd.type), "%s", "GETMETA");
    (void)snprintf(meta_cmd.id, sizeof(meta_cmd.id), "%s", "1");
    (void)snprintf(meta_cmd.username, sizeof(meta_cmd.username), "%s", "NM");
    (void)snprintf(meta_cmd.role, sizeof(meta_cmd.role), "%s", "NM");
    (void)snprintf(meta_cmd.payload, sizeof(meta_cmd.payload), "%s", entry->filename);
    
    char cmd_line[MAX_LINE];
    proto_format_line(&meta_cmd, cmd_line, sizeof(cmd_line));
    if (send_all(ss_fd, cmd_line, strlen(cmd_line)) != 0) {
        close(ss_fd);
        log_error("nm_load_owner", "Failed to send GETMETA to SS");
        return -1;
    }
    
    // Wait for response from SS
    char resp_buf[MAX_LINE];
    int n = recv_line(ss_fd, resp_buf, sizeof(resp_buf));
    close(ss_fd);
    
    if (n <= 0) {
        log_error("nm_load_owner", "No response from SS");
        return -1;
    }
    
    Message ss_resp;
    if (proto_parse_line(resp_buf, &ss_resp) != 0) {
        log_error("nm_load_owner", "Invalid response from SS");
        return -1;
    }
    
    // Check if SS returned error
    if (strcmp(ss_resp.type, "ERROR") == 0) {
        log_error("nm_load_owner", "SS returned error for file=%s", entry->filename);
        return -1;
    }
    
    // Parse metadata from payload: "owner=alice,size=100,..."
    char *owner_start = strstr(ss_resp.payload, "owner=");
    if (owner_start) {
        owner_start += 6;  // Skip "owner="
        char *owner_end = strchr(owner_start, ',');
        if (owner_end) {
            size_t owner_len = owner_end - owner_start;
            if (owner_len < sizeof(entry->owner)) {
                memcpy(entry->owner, owner_start, owner_len);
                entry->owner[owner_len] = '\0';
                log_info("nm_owner_loaded", "file=%s owner=%s", entry->filename, entry->owner);
                return 0;
            }
        } else {
            // No comma after owner (it's the last field or only field)
            strncpy(entry->owner, owner_start, sizeof(entry->owner) - 1);
            entry->owner[sizeof(entry->owner) - 1] = '\0';
            log_info("nm_owner_loaded", "file=%s owner=%s", entry->filename, entry->owner);
            return 0;
        }
    }
    
    log_error("nm_load_owner", "Owner not found in metadata for file=%s", entry->filename);
    return -1;
}

// Helper: Select SS for new file (round-robin or first available)
// Returns: SS username, or NULL if no SS available
static const char *select_ss_for_new_file(void) {
    const char *least = registry_get_least_loaded_ss();
    if (least) return least;
    return registry_get_first_ss();
}

// Handle VIEW command
int handle_view(int client_fd, const char *username, const char *flags) {
    // printf("DEBUG: handle_view called with username=%s flags=%s\n", username, flags);
    if (!username || !client_fd) {
        return -1;
    }
    
    // Parse flags
    int show_all = 0;  // -a flag
    int show_details = 0;  // -l flag
    
    if (flags) {
        if (strchr(flags, 'a')) show_all = 1;
        if (strchr(flags, 'l')) show_details = 1;
    }
    
    // Get files from index
    FileEntry *all_files[1000];
    int total_count = index_get_all_files(all_files, 1000);
    
    // Filter files based on access (if not -a)
    FileEntry *filtered_files[1000];
    int filtered_count = 0;
    
    // Load owner from SS metadata for files that don't have it set
    // printf("DEBUG: SHOW_ALL :%d total_count=%d\n", show_all, total_count);
    for (int i = 0; i < total_count; i++) {
        if (all_files[i]->owner[0] == '\0') {
            // Owner not set - load from SS metadata
            load_owner_from_ss(all_files[i]);
        }
    }
    
    if (show_all) {
        // Show all files
        for (int i = 0; i < total_count; i++) {
            filtered_files[filtered_count++] = all_files[i];
        }
    } else {
        // Show files owned by user
        // printf("DEBUG: %d\n",total_count);;
        // fflush(stdout);
        for (int i = 0; i < total_count; i++) {
            log_info("nm_view_check_owner", "file=%s owner=%s user=%s",
                     all_files[i]->filename, all_files[i]->owner, username);
            if (strcmp(all_files[i]->owner, username) == 0) {
                filtered_files[filtered_count++] = all_files[i];
            }
        }
    }
    
    // Format output
    char output[8192] = {0};
    size_t output_pos = 0;
    
    if (show_details) {
        // Table format with details
        (void)snprintf(output + output_pos, sizeof(output) - output_pos,
                      "---------------------------------------------------------\n"
                      "|  Filename  | Words | Chars | Last Access Time | Owner |\n"
                      "|------------|-------|-------|------------------|-------|\n");
        output_pos = strlen(output);
        
        for (int i = 0; i < filtered_count && output_pos < sizeof(output) - 200; i++) {
            FileEntry *f = filtered_files[i];
            char time_str[32];
            struct tm *tm = localtime(&f->last_accessed);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm);
            
            int n = snprintf(output + output_pos, sizeof(output) - output_pos,
                           "| %-10s | %5d | %5d | %-16s | %-5s |\n",
                           f->filename, f->word_count, f->char_count,
                           time_str, f->owner);
            if (n > 0) output_pos += n;
        }
        
        (void)snprintf(output + output_pos, sizeof(output) - output_pos,
                      "---------------------------------------------------------\n");
    } else {
        // Simple list format
        for (int i = 0; i < filtered_count && output_pos < sizeof(output) - 100; i++) {
            int n = snprintf(output + output_pos, sizeof(output) - output_pos,
                           "--> %s\n", filtered_files[i]->filename);
            if (n > 0) {
                output_pos += n;
            } else {
                // snprintf failed or truncated
                break;
            }
        }
        // Ensure output is null-terminated
        output[output_pos] = '\0';
    }
    
    // If no files found, show a message
    if (filtered_count == 0) {
        if (show_all) {
            (void)snprintf(output, sizeof(output), "No files found.\n");
        } else {
            (void)snprintf(output, sizeof(output), "No files found. (Use -a to view all files)\n");
        }
    }
    
    // Send response
    return send_data_response(client_fd, "", username, output);
}

// Handle CREATE command
int handle_create(int client_fd, const char *username, const char *filename) {
    if (!username || !filename || !client_fd) {
        Error err = error_simple(ERR_INVALID, "Invalid parameters");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Check if file already exists
    FileEntry *existing = index_lookup_file(filename);
    if (existing) {
        Error err = error_create(ERR_CONFLICT, "File '%s' already exists", filename);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Select SS for new file
    const char *ss_username = select_ss_for_new_file();
    if (!ss_username) {
        Error err = error_simple(ERR_UNAVAILABLE, "No storage server available");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Find SS connection
    int ss_fd = find_ss_connection(ss_username);
    if (ss_fd < 0) {
        Error err = error_simple(ERR_UNAVAILABLE, "Cannot connect to storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Send CREATE command to SS
    Message create_cmd = {0};
    (void)snprintf(create_cmd.type, sizeof(create_cmd.type), "%s", "CREATE");
    (void)snprintf(create_cmd.id, sizeof(create_cmd.id), "%s", "1");
    (void)snprintf(create_cmd.username, sizeof(create_cmd.username), "%s", username);
    (void)snprintf(create_cmd.role, sizeof(create_cmd.role), "%s", "NM");
    (void)snprintf(create_cmd.payload, sizeof(create_cmd.payload), "%s", filename);
    
    char cmd_line[MAX_LINE];
    proto_format_line(&create_cmd, cmd_line, sizeof(cmd_line));
    if (send_all(ss_fd, cmd_line, strlen(cmd_line)) != 0) {
        close(ss_fd);
        Error err = error_simple(ERR_INTERNAL, "Failed to send command to storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Wait for ACK from SS
    char resp_buf[MAX_LINE];
    int n = recv_line(ss_fd, resp_buf, sizeof(resp_buf));
    close(ss_fd);
    
    if (n <= 0) {
        Error err = error_simple(ERR_INTERNAL, "No response from storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    Message ss_resp;
    if (proto_parse_line(resp_buf, &ss_resp) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Invalid response from storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Check if SS returned error
    if (strcmp(ss_resp.type, "ERROR") == 0) {
        char error_code[64];
        char error_msg[256];
        proto_parse_error(&ss_resp, error_code, sizeof(error_code),
                         error_msg, sizeof(error_msg));
        Error err = error_simple(ERR_INTERNAL, error_msg);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // SS created file successfully - add to index or update existing entry
    // Get SS info for index
    FileEntry *entry = index_lookup_file(filename);
    
    // Get SS info from registry
    char ss_host[64] = {0};
    int ss_client_port = 0;
    
    if (registry_get_ss_info(ss_username, ss_host, sizeof(ss_host), &ss_client_port) == 0) {
        if (!entry) {
            // File not in index - add it
            entry = index_add_file(filename, username, ss_host, ss_client_port, ss_username);
        } else {
            // File exists in index (probably from SS registration with owner=ss1)
            // Update the owner to the actual creator
            size_t owner_len = strlen(username);
            size_t copy_len = (owner_len < sizeof(entry->owner) - 1) ? owner_len : sizeof(entry->owner) - 1;
            memcpy(entry->owner, username, copy_len);
            entry->owner[copy_len] = '\0';
            log_info("nm_file_owner_updated", "file=%s new_owner=%s", filename, username);
        }
    }
    
    if (entry) {
        log_info("nm_file_created", "file=%s owner=%s", filename, entry->owner);
        registry_adjust_ss_file_count(ss_username, 1);
        return send_success_response(client_fd, "", username, "File Created Successfully!");
    } else {
        Error err = error_simple(ERR_INTERNAL, "Failed to index file");
        return send_error_response(client_fd, "", username, &err);
    }
}

// Handle DELETE command
int handle_delete(int client_fd, const char *username, const char *filename) {
    if (!username || !filename || !client_fd) {
        Error err = error_simple(ERR_INVALID, "Invalid parameters");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Lookup file in index
    FileEntry *entry = index_lookup_file(filename);
    if (!entry) {
        Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", filename);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Check if user is owner
    // Note: Full ACL check would require loading metadata from SS
    // For now, we check index owner field
    if (strcmp(entry->owner, username) != 0) {
        Error err = error_create(ERR_UNAUTHORIZED, "User '%s' is not the owner of file '%s'", username, filename);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Connect to SS
    int ss_fd = get_ss_connection_for_file(entry);
    if (ss_fd < 0) {
        Error err = error_simple(ERR_UNAVAILABLE, "Cannot connect to storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Send DELETE command to SS
    Message delete_cmd = {0};
    (void)snprintf(delete_cmd.type, sizeof(delete_cmd.type), "%s", "DELETE");
    (void)snprintf(delete_cmd.id, sizeof(delete_cmd.id), "%s", "1");
    (void)snprintf(delete_cmd.username, sizeof(delete_cmd.username), "%s", username);
    (void)snprintf(delete_cmd.role, sizeof(delete_cmd.role), "%s", "NM");
    (void)snprintf(delete_cmd.payload, sizeof(delete_cmd.payload), "%s", filename);
    
    char cmd_line[MAX_LINE];
    proto_format_line(&delete_cmd, cmd_line, sizeof(cmd_line));
    if (send_all(ss_fd, cmd_line, strlen(cmd_line)) != 0) {
        close(ss_fd);
        Error err = error_simple(ERR_INTERNAL, "Failed to send command to storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Wait for ACK from SS
    char resp_buf[MAX_LINE];
    int n = recv_line(ss_fd, resp_buf, sizeof(resp_buf));
    close(ss_fd);
    
    if (n <= 0) {
        Error err = error_simple(ERR_INTERNAL, "No response from storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    Message ss_resp;
    if (proto_parse_line(resp_buf, &ss_resp) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Invalid response from storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Check if SS returned error
    if (strcmp(ss_resp.type, "ERROR") == 0) {
        char error_code[64];
        char error_msg[256];
        proto_parse_error(&ss_resp, error_code, sizeof(error_code),
                         error_msg, sizeof(error_msg));
        Error err = error_simple(ERR_INTERNAL, error_msg);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // SS deleted file successfully - remove from index
    if (index_remove_file(filename) == 0) {
        log_info("nm_file_deleted", "file=%s owner=%s", filename, username);
        registry_adjust_ss_file_count(entry->ss_username, -1);
        return send_success_response(client_fd, "", username, "File deleted successfully!");
    } else {
        Error err = error_simple(ERR_INTERNAL, "Failed to remove file from index");
        return send_error_response(client_fd, "", username, &err);
    }
}

// Handle INFO command
int handle_info(int client_fd, const char *username, const char *filename) {
    if (!username || !filename || !client_fd) {
        Error err = error_simple(ERR_INVALID, "Invalid parameters");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Lookup file in index
    FileEntry *entry = index_lookup_file(filename);
    if (!entry) {
        Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", filename);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Load owner from SS metadata if not already set
    if (entry->owner[0] == '\0') {
        load_owner_from_ss(entry);
    }
    
    // Note: Full ACL check would require loading metadata from SS
    // For now, we allow if user is owner (simplified)
    if (strcmp(entry->owner, username) != 0) {
        // Could check read access here via ACL, but for now just check owner
        // Future: Load ACL from SS metadata and check read access
    }
    
    // Update last accessed timestamp
    time_t now = time(NULL);
    index_update_metadata(filename, now, 0, entry->size_bytes,
                         entry->word_count, entry->char_count);
    
    // Format INFO output
    char output[1024] = {0};
    char created_str[32], modified_str[32], accessed_str[32];
    
    struct tm *tm = localtime(&entry->created);
    strftime(created_str, sizeof(created_str), "%Y-%m-%d %H:%M", tm);
    tm = localtime(&entry->last_modified);
    strftime(modified_str, sizeof(modified_str), "%Y-%m-%d %H:%M", tm);
    tm = localtime(&entry->last_accessed);
    strftime(accessed_str, sizeof(accessed_str), "%Y-%m-%d %H:%M", tm);
    
    (void)snprintf(output, sizeof(output),
                  "--> File: %s\n"
                  "--> Owner: %s\n"
                  "--> Created: %s\n"
                  "--> Last Modified: %s\n"
                  "--> Size: %zu bytes\n"
                  "--> Words: %d\n"
                  "--> Characters: %d\n"
                  "--> Last Accessed: %s by %s\n",
                  filename, entry->owner, created_str, modified_str,
                  entry->size_bytes, entry->word_count, entry->char_count,
                  accessed_str, username);
    
    return send_data_response(client_fd, "", username, output);
}

// Handle LIST command
int handle_list(int client_fd, const char *username) {
    // Get all registered users from registry
    char output[4096] = {0};
    size_t output_pos = 0;
    
    char clients[100][64];
    int count = registry_get_clients(clients, 100);
    
    for (int i = 0; i < count && output_pos < sizeof(output) - 100; i++) {
        int n = snprintf(output + output_pos, sizeof(output) - output_pos,
                         "--> %s\n", clients[i]);
        if (n > 0) output_pos += n;
    }
    
    return send_data_response(client_fd, "", username, output);
}

// Handle READ command - Returns SS connection info for direct client connection
int handle_read(int client_fd, const char *username, const char *filename) {
    if (!username || !filename || !client_fd) {
        Error err = error_simple(ERR_INVALID, "Invalid parameters");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Lookup file in index
    FileEntry *entry = index_lookup_file(filename);
    if (!entry) {
        Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", filename);
        return send_error_response(client_fd, "", username, &err);
    }

    // Load ACL from SS and check read access
    ACL acl = {0};
    if (fetch_acl_from_ss(entry, &acl) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Failed to load ACL");
        return send_error_response(client_fd, "", username, &err);
    }
    Error access_err = check_file_access(filename, username, 0, &acl);
    if (!error_is_ok(&access_err)) {
        return send_error_response(client_fd, "", username, &access_err);
    }
    
    // Format SS connection info: host=IP,port=PORT
    char ss_info[256];
    (void)snprintf(ss_info, sizeof(ss_info), "host=%s,port=%d", entry->ss_host, entry->ss_client_port);
    
    // Send SS_INFO message to client
    Message resp = {0};
    (void)snprintf(resp.type, sizeof(resp.type), "%s", "SS_INFO");
    (void)snprintf(resp.id, sizeof(resp.id), "%s", "");
    (void)snprintf(resp.username, sizeof(resp.username), "%s", username ? username : "");
    (void)snprintf(resp.role, sizeof(resp.role), "%s", "NM");
    (void)snprintf(resp.payload, sizeof(resp.payload), "%s", ss_info);
    
    char resp_buf[MAX_LINE];
    if (proto_format_line(&resp, resp_buf, sizeof(resp_buf)) != 0) {
        log_error("nm_read_fmt", "failed to format SS_INFO response");
        Error err = error_simple(ERR_INTERNAL, "Failed to format response");
        return send_error_response(client_fd, "", username, &err);
    }
    
    log_info("nm_read_ss_info", "file=%s user=%s ss=%s:%d", filename, username, entry->ss_host, entry->ss_client_port);
    return send_all(client_fd, resp_buf, strlen(resp_buf));
}

// Handle STREAM command - Returns SS connection info for direct client connection
int handle_stream(int client_fd, const char *username, const char *filename) {
    if (!username || !filename || !client_fd) {
        Error err = error_simple(ERR_INVALID, "Invalid parameters");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Lookup file in index
    FileEntry *entry = index_lookup_file(filename);
    if (!entry) {
        Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", filename);
        return send_error_response(client_fd, "", username, &err);
    }

    // Load ACL and check read access
    ACL acl = {0};
    if (fetch_acl_from_ss(entry, &acl) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Failed to load ACL");
        return send_error_response(client_fd, "", username, &err);
    }
    Error access_err = check_file_access(filename, username, 0, &acl);
    if (!error_is_ok(&access_err)) {
        return send_error_response(client_fd, "", username, &access_err);
    }
    
    // Format SS connection info: host=IP,port=PORT
    char ss_info[256];
    (void)snprintf(ss_info, sizeof(ss_info), "host=%s,port=%d", entry->ss_host, entry->ss_client_port);
    
    // Send SS_INFO message to client
    Message resp = {0};
    (void)snprintf(resp.type, sizeof(resp.type), "%s", "SS_INFO");
    (void)snprintf(resp.id, sizeof(resp.id), "%s", "");
    (void)snprintf(resp.username, sizeof(resp.username), "%s", username ? username : "");
    (void)snprintf(resp.role, sizeof(resp.role), "%s", "NM");
    (void)snprintf(resp.payload, sizeof(resp.payload), "%s", ss_info);
    
    char resp_buf[MAX_LINE];
    if (proto_format_line(&resp, resp_buf, sizeof(resp_buf)) != 0) {
        log_error("nm_stream_fmt", "failed to format SS_INFO response");
        Error err = error_simple(ERR_INTERNAL, "Failed to format response");
        return send_error_response(client_fd, "", username, &err);
    }
    
    log_info("nm_stream_ss_info", "file=%s user=%s ss=%s:%d", filename, username, entry->ss_host, entry->ss_client_port);
    return send_all(client_fd, resp_buf, strlen(resp_buf));
}

int handle_undo(int client_fd, const char *username, const char *filename) {
    if (!username || !filename || !client_fd) {
        Error err = error_simple(ERR_INVALID, "Invalid parameters");
        return send_error_response(client_fd, "", username, &err);
    }

    FileEntry *entry = index_lookup_file(filename);
    if (!entry) {
        Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", filename);
        return send_error_response(client_fd, "", username, &err);
    }

    ACL acl = {0};
    if (fetch_acl_from_ss(entry, &acl) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Failed to load ACL");
        return send_error_response(client_fd, "", username, &err);
    }
    Error access_err = check_file_access(filename, username, 0, &acl);
    if (!error_is_ok(&access_err)) {
        return send_error_response(client_fd, "", username, &access_err);
    }

    char ss_info[256];
    (void)snprintf(ss_info, sizeof(ss_info), "host=%s,port=%d", entry->ss_host, entry->ss_client_port);

    Message resp = {0};
    (void)snprintf(resp.type, sizeof(resp.type), "%s", "SS_INFO");
    (void)snprintf(resp.id, sizeof(resp.id), "%s", "");
    (void)snprintf(resp.username, sizeof(resp.username), "%s", username ? username : "");
    (void)snprintf(resp.role, sizeof(resp.role), "%s", "NM");
    (void)snprintf(resp.payload, sizeof(resp.payload), "%s", ss_info);

    char resp_buf[MAX_LINE];
    if (proto_format_line(&resp, resp_buf, sizeof(resp_buf)) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Failed to format response");
        return send_error_response(client_fd, "", username, &err);
    }
    log_info("nm_cmd_undo", "user=%s file=%s", username, filename);
    return send_all(client_fd, resp_buf, strlen(resp_buf));
}

int handle_write(int client_fd, const char *username, const char *filename, int sentence_index) {
    (void)sentence_index;
    if (!username || !filename || !client_fd) {
        Error err = error_simple(ERR_INVALID, "Invalid parameters");
        return send_error_response(client_fd, "", username, &err);
    }

    FileEntry *entry = index_lookup_file(filename);
    if (!entry) {
        Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", filename);
        return send_error_response(client_fd, "", username, &err);
    }

    ACL acl = {0};
    if (fetch_acl_from_ss(entry, &acl) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Failed to load ACL");
        return send_error_response(client_fd, "", username, &err);
    }
    Error access_err = check_file_access(filename, username, 1, &acl);
    if (!error_is_ok(&access_err)) {
        return send_error_response(client_fd, "", username, &access_err);
    }

    char ss_info[256];
    (void)snprintf(ss_info, sizeof(ss_info), "host=%s,port=%d", entry->ss_host, entry->ss_client_port);

    Message resp = {0};
    (void)snprintf(resp.type, sizeof(resp.type), "%s", "SS_INFO");
    (void)snprintf(resp.id, sizeof(resp.id), "%s", "");
    (void)snprintf(resp.username, sizeof(resp.username), "%s", username ? username : "");
    (void)snprintf(resp.role, sizeof(resp.role), "%s", "NM");
    (void)snprintf(resp.payload, sizeof(resp.payload), "%s", ss_info);

    char resp_buf[MAX_LINE];
    if (proto_format_line(&resp, resp_buf, sizeof(resp_buf)) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Failed to format response");
        return send_error_response(client_fd, "", username, &err);
    }
    log_info("nm_cmd_write", "file=%s user=%s", filename, username);
    return send_all(client_fd, resp_buf, strlen(resp_buf));
}

// Handle ADDACCESS command
int handle_addaccess(int client_fd, const char *username, const char *flag,
                     const char *filename, const char *target_username) {
    if (!username || !flag || !filename || !target_username || !client_fd) {
        Error err = error_simple(ERR_INVALID, "Invalid parameters");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Lookup file in index
    FileEntry *entry = index_lookup_file(filename);
    if (!entry) {
        Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", filename);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Load ACL and verify requester is owner
    ACL acl = {0};
    if (fetch_acl_from_ss(entry, &acl) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Failed to load ACL");
        return send_error_response(client_fd, "", username, &err);
    }
    Error owner_err = check_file_owner(filename, username, &acl);
    if (!error_is_ok(&owner_err)) {
        return send_error_response(client_fd, "", username, &owner_err);
    }
    
    // Connect to SS
    int ss_fd = get_ss_connection_for_file(entry);
    if (ss_fd < 0) {
        Error err = error_simple(ERR_UNAVAILABLE, "Cannot connect to storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Send UPDATE_ACL command to SS
    Message update_cmd = {0};
    (void)snprintf(update_cmd.type, sizeof(update_cmd.type), "%s", "UPDATE_ACL");
    (void)snprintf(update_cmd.id, sizeof(update_cmd.id), "%s", "1");
    (void)snprintf(update_cmd.username, sizeof(update_cmd.username), "%s", username);
    (void)snprintf(update_cmd.role, sizeof(update_cmd.role), "%s", "NM");
    
    // Payload: action=ADD,flag=R|W,filename=FILE,target_user=USER
    (void)snprintf(update_cmd.payload, sizeof(update_cmd.payload),
                   "action=ADD,flag=%s,filename=%s,target_user=%s", flag, filename, target_username);
    
    char cmd_line[MAX_LINE];
    proto_format_line(&update_cmd, cmd_line, sizeof(cmd_line));
    if (send_all(ss_fd, cmd_line, strlen(cmd_line)) != 0) {
        close(ss_fd);
        Error err = error_simple(ERR_INTERNAL, "Failed to send command to storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Wait for ACK from SS
    char resp_buf[MAX_LINE];
    int n = recv_line(ss_fd, resp_buf, sizeof(resp_buf));
    close(ss_fd);
    
    if (n <= 0) {
        Error err = error_simple(ERR_INTERNAL, "No response from storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    Message ss_resp;
    if (proto_parse_line(resp_buf, &ss_resp) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Invalid response from storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Check if SS returned error
    if (strcmp(ss_resp.type, "ERROR") == 0) {
        char error_code[64];
        char error_msg[256];
        proto_parse_error(&ss_resp, error_code, sizeof(error_code),
                         error_msg, sizeof(error_msg));
        Error err = error_simple(ERR_INTERNAL, error_msg);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Success
    log_info("nm_access_granted", "file=%s owner=%s target=%s flag=%s", 
             filename, username, target_username, flag);
    return send_success_response(client_fd, "", username, "Access granted successfully!");
}

// Handle REMACCESS command
int handle_remaccess(int client_fd, const char *username,
                     const char *filename, const char *target_username) {
    if (!username || !filename || !target_username || !client_fd) {
        Error err = error_simple(ERR_INVALID, "Invalid parameters");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Lookup file in index
    FileEntry *entry = index_lookup_file(filename);
    if (!entry) {
        Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", filename);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Load ACL and verify requester is owner
    ACL acl = {0};
    if (fetch_acl_from_ss(entry, &acl) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Failed to load ACL");
        return send_error_response(client_fd, "", username, &err);
    }
    Error owner_err = check_file_owner(filename, username, &acl);
    if (!error_is_ok(&owner_err)) {
        return send_error_response(client_fd, "", username, &owner_err);
    }
    
    // Connect to SS
    int ss_fd = get_ss_connection_for_file(entry);
    if (ss_fd < 0) {
        Error err = error_simple(ERR_UNAVAILABLE, "Cannot connect to storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Send UPDATE_ACL command to SS
    Message update_cmd = {0};
    (void)snprintf(update_cmd.type, sizeof(update_cmd.type), "%s", "UPDATE_ACL");
    (void)snprintf(update_cmd.id, sizeof(update_cmd.id), "%s", "1");
    (void)snprintf(update_cmd.username, sizeof(update_cmd.username), "%s", username);
    (void)snprintf(update_cmd.role, sizeof(update_cmd.role), "%s", "NM");
    
    // Payload: action=REMOVE,flag=,filename=FILE,target_user=USER
    (void)snprintf(update_cmd.payload, sizeof(update_cmd.payload),
                   "action=REMOVE,flag=,filename=%s,target_user=%s", filename, target_username);
    
    char cmd_line[MAX_LINE];
    proto_format_line(&update_cmd, cmd_line, sizeof(cmd_line));
    if (send_all(ss_fd, cmd_line, strlen(cmd_line)) != 0) {
        close(ss_fd);
        Error err = error_simple(ERR_INTERNAL, "Failed to send command to storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Wait for ACK from SS
    char resp_buf[MAX_LINE];
    int n = recv_line(ss_fd, resp_buf, sizeof(resp_buf));
    close(ss_fd);
    
    if (n <= 0) {
        Error err = error_simple(ERR_INTERNAL, "No response from storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    Message ss_resp;
    if (proto_parse_line(resp_buf, &ss_resp) != 0) {
        Error err = error_simple(ERR_INTERNAL, "Invalid response from storage server");
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Check if SS returned error
    if (strcmp(ss_resp.type, "ERROR") == 0) {
        char error_code[64];
        char error_msg[256];
        proto_parse_error(&ss_resp, error_code, sizeof(error_code),
                         error_msg, sizeof(error_msg));
        Error err = error_simple(ERR_INTERNAL, error_msg);
        return send_error_response(client_fd, "", username, &err);
    }
    
    // Success
    log_info("nm_access_removed", "file=%s owner=%s target=%s", 
             filename, username, target_username);
    return send_success_response(client_fd, "", username, "Access removed successfully!");
}

