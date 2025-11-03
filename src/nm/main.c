// Name Server (NM): accepts connections from SS/Clients and handles
// registration and heartbeats in Phase 1. Thread-per-connection model.
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

// Argument passed to each connection handler thread.
typedef struct ClientConnArg {
    int fd;
    struct sockaddr_in addr;
} ClientConnArg;

// Extremely simple in-memory registry for demo purposes.
typedef struct RegistryEntry {
    char role[16]; // SS or CLIENT
    char username[64];
    char payload[256]; // For SS: "nm_port,client_port,files..." simplified
    struct RegistryEntry *next;
} RegistryEntry;

static RegistryEntry *g_registry_head = NULL;
static pthread_mutex_t g_registry_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_running = 1;

// Add an entry to the registry (not de-duplicated in Phase 1).
static void registry_add(const char *role, const char *username, const char *payload) {
    RegistryEntry *e = (RegistryEntry*)calloc(1, sizeof(RegistryEntry));
    (void)snprintf(e->role, sizeof(e->role), "%s", role ? role : "");
    (void)snprintf(e->username, sizeof(e->username), "%s", username ? username : "");
    (void)snprintf(e->payload, sizeof(e->payload), "%s", payload ? payload : "");
    pthread_mutex_lock(&g_registry_mu);
    e->next = g_registry_head;
    g_registry_head = e;
    pthread_mutex_unlock(&g_registry_mu);
}

// Handle a single parsed message from a peer.
static void handle_message(int fd, const struct sockaddr_in *peer, const Message *msg) {
    char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &peer->sin_addr, ip, sizeof(ip));
    if (strcmp(msg->type, "SS_REGISTER") == 0) {
        // Phase 2: Parse file list from SS registration payload
        // Payload format: "host=IP,client_port=PORT,storage=DIR,files=file1.txt,file2.txt,..."
        // Extract file list for logging (full indexing will be in Step 3)
        char *files_start = strstr(msg->payload, "files=");
        int file_count = 0;
        if (files_start) {
            files_start += 6;  // Skip "files="
            if (*files_start != '\0') {
                // Count files (comma-separated)
                char *p = files_start;
                file_count = 1;  // At least one file
                while (*p) {
                    if (*p == ',') file_count++;
                    p++;
                }
            }
        }
        
        registry_add("SS", msg->username, msg->payload);
        log_info("nm_ss_register", "ip=%s user=%s files=%d", ip, msg->username, file_count);
        
        // Phase 2: Log file list for verification
        if (files_start && *files_start != '\0') {
            log_info("nm_ss_file_list", "user=%s list=%s", msg->username, files_start);
        }
        
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
    // Unknown
    log_error("nm_unknown_msg", "type=%s", msg->type);
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


