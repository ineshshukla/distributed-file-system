#include "registry.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global registry (thread-safe)
static RegistryEntry *g_registry_head = NULL;
static pthread_mutex_t g_registry_mu = PTHREAD_MUTEX_INITIALIZER;

// Add entry to registry
void registry_add(const char *role, const char *username, const char *payload) {
    if (!role || !username) return;
    pthread_mutex_lock(&g_registry_mu);
    RegistryEntry *entry = g_registry_head;
    while (entry) {
        if (strcmp(entry->role, role) == 0 &&
            strcmp(entry->username, username) == 0) {
            snprintf(entry->payload, sizeof(entry->payload), "%s", payload ? payload : "");
            pthread_mutex_unlock(&g_registry_mu);
            return;
        }
        entry = entry->next;
    }
    RegistryEntry *e = (RegistryEntry*)calloc(1, sizeof(RegistryEntry));
    snprintf(e->role, sizeof(e->role), "%s", role);
    snprintf(e->username, sizeof(e->username), "%s", username);
    snprintf(e->payload, sizeof(e->payload), "%s", payload ? payload : "");
    e->file_count = 0;
    e->next = g_registry_head;
    g_registry_head = e;
    pthread_mutex_unlock(&g_registry_mu);
}

// Get first SS entry
const char *registry_get_first_ss(void) {
    static char selected_ss[64] = {0};
    pthread_mutex_lock(&g_registry_mu);
    RegistryEntry *entry = g_registry_head;
    while (entry) {
        if (strcmp(entry->role, "SS") == 0) {
            size_t username_len = strlen(entry->username);
            size_t copy_len = (username_len < sizeof(selected_ss) - 1) ? username_len : sizeof(selected_ss) - 1;
            memcpy(selected_ss, entry->username, copy_len);
            selected_ss[copy_len] = '\0';
            pthread_mutex_unlock(&g_registry_mu);
            return selected_ss;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&g_registry_mu);
    return NULL;
}

const char *registry_get_least_loaded_ss(void) {
    static char selected_ss[64] = {0};
    pthread_mutex_lock(&g_registry_mu);
    RegistryEntry *entry = g_registry_head;
    int best_count = -1;
    const char *best_username = NULL;
    while (entry) {
        if (strcmp(entry->role, "SS") == 0) {
            if (best_username == NULL || entry->file_count < best_count) {
                best_username = entry->username;
                best_count = entry->file_count;
            }
        }
        entry = entry->next;
    }
    if (best_username) {
        strncpy(selected_ss, best_username, sizeof(selected_ss) - 1);
        selected_ss[sizeof(selected_ss) - 1] = '\0';
        pthread_mutex_unlock(&g_registry_mu);
        return selected_ss;
    }
    pthread_mutex_unlock(&g_registry_mu);
    return NULL;
}

// Get SS info by username
int registry_get_ss_info(const char *ss_username, char *host, size_t host_len,
                         int *client_port) {
    if (!ss_username || !host || !client_port) return -1;
    
    pthread_mutex_lock(&g_registry_mu);
    RegistryEntry *entry = g_registry_head;
    while (entry) {
        if (strcmp(entry->role, "SS") == 0 &&
            strcmp(entry->username, ss_username) == 0) {
            // Parse payload
            char *host_start = strstr(entry->payload, "host=");
            char *port_start = strstr(entry->payload, "client_port=");
            
            if (host_start) {
                char *host_end = strchr(host_start + 5, ',');
                if (host_end) {
                    size_t host_str_len = host_end - (host_start + 5);
                    size_t copy_len = (host_str_len < host_len - 1) ? host_str_len : host_len - 1;
                    memcpy(host, host_start + 5, copy_len);
                    host[copy_len] = '\0';
                }
            }
            
            if (port_start) {
                *client_port = atoi(port_start + 12);
            }
            
            pthread_mutex_unlock(&g_registry_mu);
            return 0;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&g_registry_mu);
    return -1;
}

// Get all client usernames
int registry_get_clients(char clients[][64], int max_clients) {
    int count = 0;
    pthread_mutex_lock(&g_registry_mu);
    RegistryEntry *entry = g_registry_head;
    while (entry && count < max_clients) {
        if (strcmp(entry->role, "CLIENT") == 0) {
            size_t username_len = strlen(entry->username);
            size_t copy_len = (username_len < 63) ? username_len : 63;
            memcpy(clients[count], entry->username, copy_len);
            clients[count][copy_len] = '\0';
            count++;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&g_registry_mu);
    return count;
}

void registry_set_ss_file_count(const char *ss_username, int count) {
    pthread_mutex_lock(&g_registry_mu);
    RegistryEntry *entry = g_registry_head;
    while (entry) {
        if (strcmp(entry->role, "SS") == 0 &&
            strcmp(entry->username, ss_username) == 0) {
            entry->file_count = count;
            break;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&g_registry_mu);
}

void registry_adjust_ss_file_count(const char *ss_username, int delta) {
    pthread_mutex_lock(&g_registry_mu);
    RegistryEntry *entry = g_registry_head;
    while (entry) {
        if (strcmp(entry->role, "SS") == 0 &&
            strcmp(entry->username, ss_username) == 0) {
            entry->file_count += delta;
            if (entry->file_count < 0) entry->file_count = 0;
            break;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&g_registry_mu);
}

