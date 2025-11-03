#include "index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Global index and cache instances
FileIndex g_file_index = {0};
LRUCache g_lru_cache = {0};

// Initialize the file index and LRU cache
// Sets up empty hash table and empty cache
void index_init(void) {
    // Clear hash table (all buckets are NULL)
    memset(g_file_index.buckets, 0, sizeof(g_file_index.buckets));
    g_file_index.count = 0;
    
    // Clear LRU cache
    g_lru_cache.head = NULL;
    g_lru_cache.tail = NULL;
    g_lru_cache.size = 0;
}

// Hash function: djb2 hash algorithm (simple and effective)
// Converts filename string to hash value for bucket selection
unsigned int index_hash(const char *filename) {
    unsigned long hash = 5381;
    int c;
    
    // Hash each character: hash = hash * 33 + c
    while ((c = *filename++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    
    // Modulo to get bucket index (0 to INDEX_HASH_SIZE-1)
    return hash % INDEX_HASH_SIZE;
}

// Add entry to LRU cache (at front - most recently used)
// This is called when a file is accessed (looked up)
static void lru_add_to_front(FileEntry *entry) {
    if (!entry) return;
    
    // Remove from current position if already in cache
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else if (g_lru_cache.head == entry) {
        g_lru_cache.head = entry->lru_next;
    }
    
    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else if (g_lru_cache.tail == entry) {
        g_lru_cache.tail = entry->lru_prev;
    }
    
    // Add to front
    entry->lru_prev = NULL;
    entry->lru_next = g_lru_cache.head;
    
    if (g_lru_cache.head) {
        g_lru_cache.head->lru_prev = entry;
    }
    g_lru_cache.head = entry;
    
    if (!g_lru_cache.tail) {
        g_lru_cache.tail = entry;
    }
    
    g_lru_cache.size++;
}

// Remove least recently used entry from cache (when cache is full)
static void lru_remove_tail(void) {
    if (!g_lru_cache.tail) return;
    
    FileEntry *old_tail = g_lru_cache.tail;
    g_lru_cache.tail = old_tail->lru_prev;
    
    if (g_lru_cache.tail) {
        g_lru_cache.tail->lru_next = NULL;
    } else {
        g_lru_cache.head = NULL;
    }
    
    old_tail->lru_prev = NULL;
    old_tail->lru_next = NULL;
    g_lru_cache.size--;
}

// Add a file to the index
FileEntry *index_add_file(const char *filename, const char *owner,
                          const char *ss_host, int ss_client_port,
                          const char *ss_username) {
    if (!filename) return NULL;
    
    // Check if file already exists
    FileEntry *existing = index_lookup_file(filename);
    if (existing) {
        // Update SS information (in case SS re-registered)
        if (ss_host) strncpy(existing->ss_host, ss_host, sizeof(existing->ss_host) - 1);
        existing->ss_client_port = ss_client_port;
        if (ss_username) strncpy(existing->ss_username, ss_username, sizeof(existing->ss_username) - 1);
        return existing;
    }
    
    // Create new file entry
    FileEntry *entry = (FileEntry *)calloc(1, sizeof(FileEntry));
    if (!entry) return NULL;
    
    // Copy filename
    strncpy(entry->filename, filename, sizeof(entry->filename) - 1);
    
    // Copy owner
    if (owner) {
        strncpy(entry->owner, owner, sizeof(entry->owner) - 1);
    }
    
    // Copy SS information
    if (ss_host) {
        strncpy(entry->ss_host, ss_host, sizeof(entry->ss_host) - 1);
    }
    entry->ss_client_port = ss_client_port;
    if (ss_username) {
        strncpy(entry->ss_username, ss_username, sizeof(entry->ss_username) - 1);
    }
    
    // Initialize timestamps
    time_t now = time(NULL);
    entry->created = now;
    entry->last_modified = now;
    entry->last_accessed = now;
    
    // Initialize counts
    entry->size_bytes = 0;
    entry->word_count = 0;
    entry->char_count = 0;
    
    // Add to hash map
    unsigned int hash = index_hash(filename);
    entry->next = g_file_index.buckets[hash];
    g_file_index.buckets[hash] = entry;
    g_file_index.count++;
    
    return entry;
}

// Remove a file from the index
int index_remove_file(const char *filename) {
    if (!filename) return -1;
    
    unsigned int hash = index_hash(filename);
    FileEntry *curr = g_file_index.buckets[hash];
    FileEntry *prev = NULL;
    
    // Search for file in hash bucket chain
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            // Found it - remove from chain
            if (prev) {
                prev->next = curr->next;
            } else {
                g_file_index.buckets[hash] = curr->next;
            }
            
            // Remove from LRU cache if present
            if (curr->lru_prev || curr->lru_next || g_lru_cache.head == curr) {
                if (curr->lru_prev) {
                    curr->lru_prev->lru_next = curr->lru_next;
                } else {
                    g_lru_cache.head = curr->lru_next;
                }
                if (curr->lru_next) {
                    curr->lru_next->lru_prev = curr->lru_prev;
                } else {
                    g_lru_cache.tail = curr->lru_prev;
                }
                if (g_lru_cache.head == curr) g_lru_cache.head = NULL;
                if (g_lru_cache.tail == curr) g_lru_cache.tail = NULL;
                g_lru_cache.size--;
            }
            
            free(curr);
            g_file_index.count--;
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }
    
    return -1;  // Not found
}

// Lookup a file in the index (O(1) average case)
FileEntry *index_lookup_file(const char *filename) {
    if (!filename) return NULL;
    
    unsigned int hash = index_hash(filename);
    FileEntry *curr = g_file_index.buckets[hash];
    
    // Search chain for matching filename
    while (curr) {
        if (strcmp(curr->filename, filename) == 0) {
            // Found - update LRU cache
            // If cache is full, remove least recently used
            if (g_lru_cache.size >= LRU_CACHE_SIZE && 
                (!curr->lru_prev && !curr->lru_next && g_lru_cache.head != curr)) {
                lru_remove_tail();
            }
            lru_add_to_front(curr);
            return curr;
        }
        curr = curr->next;
    }
    
    return NULL;  // Not found
}

// Get all files in the index
int index_get_all_files(FileEntry **files, int max_files) {
    if (!files || max_files <= 0) return 0;
    
    int count = 0;
    
    // Iterate through all hash buckets
    for (int i = 0; i < INDEX_HASH_SIZE && count < max_files; i++) {
        FileEntry *curr = g_file_index.buckets[i];
        while (curr && count < max_files) {
            files[count++] = curr;
            curr = curr->next;
        }
    }
    
    return count;
}

// Get files owned by a specific user
int index_get_files_by_owner(const char *owner, FileEntry **files, int max_files) {
    if (!owner || !files || max_files <= 0) return 0;
    
    int count = 0;
    
    // Iterate through all hash buckets
    for (int i = 0; i < INDEX_HASH_SIZE && count < max_files; i++) {
        FileEntry *curr = g_file_index.buckets[i];
        while (curr && count < max_files) {
            // Filter by owner
            if (strcmp(curr->owner, owner) == 0) {
                files[count++] = curr;
            }
            curr = curr->next;
        }
    }
    
    return count;
}

// Update file metadata in index
int index_update_metadata(const char *filename, time_t last_accessed,
                          time_t last_modified, size_t size_bytes,
                          int word_count, int char_count) {
    FileEntry *entry = index_lookup_file(filename);
    if (!entry) return -1;
    
    if (last_accessed > 0) entry->last_accessed = last_accessed;
    if (last_modified > 0) entry->last_modified = last_modified;
    entry->size_bytes = size_bytes;
    entry->word_count = word_count;
    entry->char_count = char_count;
    
    return 0;
}

