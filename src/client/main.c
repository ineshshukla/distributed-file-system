// Client: registers its username with the NM and exits.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/net.h"
#include "../common/log.h"
#include "../common/protocol.h"

int main(int argc, char **argv) {
    const char *nm_host = "127.0.0.1"; int nm_port = 5000; const char *username = "alice";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--nm-host") && i+1 < argc) nm_host = argv[++i];
        else if (!strcmp(argv[i], "--nm-port") && i+1 < argc) nm_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--username") && i+1 < argc) username = argv[++i];
    }
    int fd = connect_to_host(nm_host, nm_port);
    if (fd < 0) { perror("connect nm"); return 1; }
    Message msg = {0};
    (void)snprintf(msg.type, sizeof(msg.type), "%s", "CLIENT_REGISTER");
    (void)snprintf(msg.id, sizeof(msg.id), "%s", "1");
    (void)snprintf(msg.username, sizeof(msg.username), "%s", username);
    (void)snprintf(msg.role, sizeof(msg.role), "%s", "CLIENT");
    msg.payload[0] = '\0';
    char line[MAX_LINE]; proto_format_line(&msg, line, sizeof(line));
    send_all(fd, line, strlen(line));
    char rbuf[MAX_LINE];
    if (recv_line(fd, rbuf, sizeof(rbuf)) > 0) {
        Message ack; if (proto_parse_line(rbuf, &ack) == 0) {
            log_info("client_registered", "user=%s reply=%s", username, ack.payload);
        }
    }
    close(fd);
    return 0;
}


