// Client: Interactive shell for file operations
// Phase 2: Supports VIEW, CREATE, READ, DELETE, INFO, LIST commands
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/net.h"
#include "../common/log.h"
#include "../common/protocol.h"
#include "../common/errors.h"
#include "commands.h"

// Global: connection to NM (kept open for entire session)
static int g_nm_fd = -1;
static char g_username[64] = {0};

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
        return -1;
    }
    
    // Parse response
    Message resp;
    if (proto_parse_line(resp_buf, &resp) != 0) {
        printf("Error: Failed to parse response\n");
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
    } else if (strcmp(resp.type, "ACK") == 0) {
        // Success response
        printf("%s\n", resp.payload);
    } else {
        // Data response (for VIEW, LIST, INFO, etc.)
        // Payload contains the actual data
        printf("%s", resp.payload);
        // If payload doesn't end with newline, add one
        size_t payload_len = strlen(resp.payload);
        if (payload_len == 0 || resp.payload[payload_len-1] != '\n') {
            printf("\n");
        }
    }
    
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
