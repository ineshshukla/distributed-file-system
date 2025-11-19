// Client: Interactive shell for file operations
// Phase 2: Supports VIEW, CREATE, READ, DELETE, INFO, LIST commands
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../common/net.h"
#include "../common/log.h"
#include "../common/protocol.h"
#include "../common/errors.h"
#include "commands.h"

// Global: connection to NM (kept open for entire session)
static int g_nm_fd = -1;
static char g_username[64] = {0};

// Forward declaration
static int handle_ss_command(const ParsedCommand *cmd, const char *ss_host, int ss_port);

// Send command to NM and receive response
// Returns: 0 on success, -1 on error
static int send_command_and_receive(const ParsedCommand *cmd) {
    if (!cmd || g_nm_fd < 0) return -1;
    
    // Format command message
    char msg_buf[MAX_LINE];
    if (format_command_message(cmd, g_username, msg_buf, sizeof(msg_buf)) != 0) {
        printf("Error: Failed to format command\n");
        return -1;
    }
    
    // Send to NM
    if (send_all(g_nm_fd, msg_buf, strlen(msg_buf)) != 0) {
        printf("Error: Failed to send command to NM\n");
        return -1;
    }
    
    // Receive response
    char resp_buf[MAX_LINE];
    int n = recv_line(g_nm_fd, resp_buf, sizeof(resp_buf));
    if (n <= 0) {
        printf("Error: Failed to receive response from NM\n");
        fflush(stdout);
        return -1;
    }
    
    // Parse response
    Message resp;
    if (proto_parse_line(resp_buf, &resp) != 0) {
        printf("Error: Failed to parse response\n");
        fflush(stdout);
        return -1;
    }
    
    // Handle response based on type
    if (strcmp(resp.type, "ERROR") == 0) {
        // Error response - parse error code and message
        char error_code[64];
        char error_msg[256];
        if (proto_parse_error(&resp, error_code, sizeof(error_code),
                              error_msg, sizeof(error_msg)) == 0) {
            printf("ERROR [%s]: %s\n", error_code, error_msg);
        } else {
            printf("ERROR: %s\n", resp.payload);
        }
        fflush(stdout);
    } else if (strcmp(resp.type, "ACK") == 0) {
        // Success response - print payload if non-empty, otherwise print a generic success message
        if (strlen(resp.payload) > 0) {
            printf("%s\n", resp.payload);
        } else {
            printf("Success\n");
        }
        fflush(stdout);
    } else if (strcmp(resp.type, "DATA") == 0) {
        // Data response (for VIEW, LIST, INFO, etc.)
        // Payload contains the actual data
        // Convert \x01 (SOH) back to \n (newline) - these were escaped to avoid breaking the line-based protocol
        if (strlen(resp.payload) > 0) {
            // Convert \x01 back to \n
            char *p = resp.payload;
            while (*p) {
                if (*p == '\x01') {
                    putchar('\n');
                } else {
                    putchar(*p);
                }
                p++;
            }
            // If payload doesn't end with newline, add one
            if (resp.payload[strlen(resp.payload) - 1] != '\x01' && resp.payload[strlen(resp.payload) - 1] != '\n') {
                printf("\n");
            }
        } else {
            // Empty data response - at least print something
            printf("(No data)\n");
        }
        fflush(stdout);
    } else if (strcmp(resp.type, "SS_INFO") == 0) {
        // SS_INFO response - contains SS connection info for direct connection
        // Payload format: "host=IP,port=PORT"
        // Parse and connect to SS
        char ss_host[64] = {0};
        int ss_port = 0;
        
        // Parse payload: "host=IP,port=PORT"
        char *host_start = strstr(resp.payload, "host=");
        char *port_start = strstr(resp.payload, "port=");
        
        if (host_start) {
            char *host_end = strchr(host_start + 5, ',');
            if (host_end) {
                size_t host_len = host_end - (host_start + 5);
                if (host_len < sizeof(ss_host)) {
                    memcpy(ss_host, host_start + 5, host_len);
                    ss_host[host_len] = '\0';
                }
            } else {
                // No comma, host is at end
                size_t host_len = strlen(host_start + 5);
                if (host_len < sizeof(ss_host)) {
                    memcpy(ss_host, host_start + 5, host_len);
                    ss_host[host_len] = '\0';
                }
            }
        }
        
        if (port_start) {
            ss_port = atoi(port_start + 5);
        }
        
        if (strlen(ss_host) > 0 && ss_port > 0) {
            // Connect to SS and handle command
            return handle_ss_command(cmd, ss_host, ss_port);
        } else {
            printf("Error: Invalid SS connection info\n");
            fflush(stdout);
            return -1;
        }
    } else {
        // Unknown response type
        printf("Response: type=%s payload=%s\n", resp.type, resp.payload);
        fflush(stdout);
    }
    
    return 0;
}

// Handle direct SS command (READ, STREAM)
// Connects to SS, sends command, receives data until STOP
// Returns: 0 on success, -1 on error
static int handle_ss_command(const ParsedCommand *cmd, const char *ss_host, int ss_port) {
    if (!cmd || !ss_host || ss_port <= 0) return -1;
    
    // Connect to SS
    int ss_fd = connect_to_host(ss_host, ss_port);
    if (ss_fd < 0) {
        printf("Error: Failed to connect to storage server at %s:%d\n", ss_host, ss_port);
        fflush(stdout);
        return -1;
    }
    
    // Format command message for SS
    Message ss_cmd = {0};
    size_t cmd_len = strlen(cmd->cmd);
    size_t copy_len = (cmd_len < sizeof(ss_cmd.type) - 1) ? cmd_len : sizeof(ss_cmd.type) - 1;
    memcpy(ss_cmd.type, cmd->cmd, copy_len);
    ss_cmd.type[copy_len] = '\0';
    
    (void)snprintf(ss_cmd.id, sizeof(ss_cmd.id), "%ld", (long)time(NULL));
    
    size_t username_len = strlen(g_username);
    copy_len = (username_len < sizeof(ss_cmd.username) - 1) ? username_len : sizeof(ss_cmd.username) - 1;
    memcpy(ss_cmd.username, g_username, copy_len);
    ss_cmd.username[copy_len] = '\0';
    
    memcpy(ss_cmd.role, "CLIENT", 6);
    ss_cmd.role[6] = '\0';
    
    // Payload is filename (first argument)
    if (cmd->argc > 0) {
        size_t arg_len = strlen(cmd->args[0]);
        copy_len = (arg_len < sizeof(ss_cmd.payload) - 1) ? arg_len : sizeof(ss_cmd.payload) - 1;
        memcpy(ss_cmd.payload, cmd->args[0], copy_len);
        ss_cmd.payload[copy_len] = '\0';
    } else {
        ss_cmd.payload[0] = '\0';
    }
    
    // Send command to SS
    char cmd_buf[MAX_LINE];
    if (proto_format_line(&ss_cmd, cmd_buf, sizeof(cmd_buf)) != 0) {
        printf("Error: Failed to format command for SS\n");
        fflush(stdout);
        close(ss_fd);
        return -1;
    }
    
    if (send_all(ss_fd, cmd_buf, strlen(cmd_buf)) != 0) {
        printf("Error: Failed to send command to SS\n");
        fflush(stdout);
        close(ss_fd);
        return -1;
    }
    
    // Handle response based on command type
    if (strcmp(cmd->cmd, "READ") == 0) {
        // Receive data until STOP packet
        while (1) {
            char resp_buf[MAX_LINE];
            int n = recv_line(ss_fd, resp_buf, sizeof(resp_buf));
            if (n <= 0) {
                printf("Error: Connection closed unexpectedly\n");
                fflush(stdout);
                close(ss_fd);
                return -1;
            }
            
            // Parse response
            Message resp;
            if (proto_parse_line(resp_buf, &resp) != 0) {
                printf("Error: Failed to parse response from SS\n");
                fflush(stdout);
                close(ss_fd);
                return -1;
            }
            
            // Check for STOP packet
            if (strcmp(resp.type, "STOP") == 0) {
                // End of data
                break;
            }
            
            // Check for ERROR
            if (strcmp(resp.type, "ERROR") == 0) {
                char error_code[64];
                char error_msg[256];
                if (proto_parse_error(&resp, error_code, sizeof(error_code),
                                      error_msg, sizeof(error_msg)) == 0) {
                    printf("ERROR [%s]: %s\n", error_code, error_msg);
                } else {
                    printf("ERROR: %s\n", resp.payload);
                }
                fflush(stdout);
                close(ss_fd);
                return -1;
            }
            
            // Handle DATA packet
            if (strcmp(resp.type, "DATA") == 0) {
                // Convert \x01 (SOH) back to \n
                if (strlen(resp.payload) > 0) {
                    char *p = resp.payload;
                    while (*p) {
                        if (*p == '\x01') {
                            putchar('\n');
                        } else {
                            putchar(*p);
                        }
                        p++;
                    }
                }
            }
        }
        
        // Add final newline if needed
        printf("\n");
        fflush(stdout);
    } else if (strcmp(cmd->cmd, "STREAM") == 0) {
        // Receive words until STOP packet
        int first_word = 1;
        while (1) {
            char resp_buf[MAX_LINE];
            int n = recv_line(ss_fd, resp_buf, sizeof(resp_buf));
            if (n <= 0) {
                printf("\nError: Connection closed unexpectedly\n");
                fflush(stdout);
                close(ss_fd);
                return -1;
            }
            
            // Parse response
            Message resp;
            if (proto_parse_line(resp_buf, &resp) != 0) {
                printf("\nError: Failed to parse response from SS\n");
                fflush(stdout);
                close(ss_fd);
                return -1;
            }
            
            // Check for STOP packet
            if (strcmp(resp.type, "STOP") == 0) {
                // End of stream
                printf("\n");
                fflush(stdout);
                break;
            }
            
            // Check for ERROR
            if (strcmp(resp.type, "ERROR") == 0) {
                char error_code[64];
                char error_msg[256];
                if (proto_parse_error(&resp, error_code, sizeof(error_code),
                                      error_msg, sizeof(error_msg)) == 0) {
                    printf("\nERROR [%s]: %s\n", error_code, error_msg);
                } else {
                    printf("\nERROR: %s\n", resp.payload);
                }
                fflush(stdout);
                close(ss_fd);
                return -1;
            }
            
            // Handle DATA packet (word)
            if (strcmp(resp.type, "DATA") == 0) {
                // Print word with space before it (except first word)
                if (!first_word) {
                    putchar(' ');
                }
                printf("%s", resp.payload);
                fflush(stdout);
                first_word = 0;
            }
        }
    }
    
    close(ss_fd);
    return 0;
}

// Interactive command loop
// Reads commands from stdin until EOF or EXIT command
static void command_loop(void) {
    char line[MAX_CMD_LINE];
    
    printf("LangOS Client - Type commands (or 'EXIT' to quit)\n");
    printf("> ");
    fflush(stdout);
    
    while (fgets(line, sizeof(line), stdin)) {
        // Parse command
        ParsedCommand cmd = parse_command(line);
        
        // Check for empty command
        if (strlen(cmd.cmd) == 0) {
            printf("> ");
            fflush(stdout);
            continue;
        }
        
        // Check for EXIT command
        if (strcmp(cmd.cmd, "EXIT") == 0) {
            printf("Exiting...\n");
            break;
        }
        
        // Send command and receive response
        send_command_and_receive(&cmd);
        
        printf("> ");
        fflush(stdout);
    }
}

int main(int argc, char **argv) {
    // Parse command line arguments
    const char *nm_host = "127.0.0.1";
    int nm_port = 5000;
    const char *username = "alice";
    
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--nm-host") && i+1 < argc) {
            nm_host = argv[++i];
        } else if (!strcmp(argv[i], "--nm-port") && i+1 < argc) {
            nm_port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--username") && i+1 < argc) {
            username = argv[++i];
        }
    }
    
    // Store username
    strncpy(g_username, username, sizeof(g_username) - 1);
    
    // Connect to NM
    g_nm_fd = connect_to_host(nm_host, nm_port);
    if (g_nm_fd < 0) {
        perror("Error: Failed to connect to Name Server");
        return 1;
    }
    
    // Register with NM
    Message reg_msg = {0};
    (void)snprintf(reg_msg.type, sizeof(reg_msg.type), "%s", "CLIENT_REGISTER");
    (void)snprintf(reg_msg.id, sizeof(reg_msg.id), "%s", "1");
    (void)snprintf(reg_msg.username, sizeof(reg_msg.username), "%s", username);
    (void)snprintf(reg_msg.role, sizeof(reg_msg.role), "%s", "CLIENT");
    reg_msg.payload[0] = '\0';
    
    char reg_line[MAX_LINE];
    proto_format_line(&reg_msg, reg_line, sizeof(reg_line));
    send_all(g_nm_fd, reg_line, strlen(reg_line));
    
    // Wait for ACK
    char ack_buf[MAX_LINE];
    if (recv_line(g_nm_fd, ack_buf, sizeof(ack_buf)) > 0) {
        Message ack;
        if (proto_parse_line(ack_buf, &ack) == 0) {
            log_info("client_registered", "user=%s", username);
        }
    }
    
    // Enter interactive command loop
    command_loop();
    
    // Cleanup
    close(g_nm_fd);
    return 0;
}
