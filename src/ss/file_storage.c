#include "file_storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Helper: Ensure directories exist (create if they don't)
static void ensure_directories(const char *storage_dir) {
    char files_dir[512];
    char meta_dir[512];
    
    snprintf(files_dir, sizeof(files_dir), "%s/files", storage_dir);
    snprintf(meta_dir, sizeof(meta_dir), "%s/metadata", storage_dir);
    
    // Create directories (mkdir -p equivalent)
    // mkdir() returns 0 on success, -1 if directory exists or error
    mkdir(storage_dir, 0755);
    mkdir(files_dir, 0755);
    mkdir(meta_dir, 0755);
}

// Create an empty file with metadata
int file_create(const char *storage_dir, const char *filename, const char *owner) {
    if (!storage_dir || !filename || !owner) return -1;
    
    // Ensure directories exist
    ensure_directories(storage_dir);
    
    // Build paths
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/files/%s", storage_dir, filename);
    
    // Check if file already exists
    if (access(file_path, F_OK) == 0) {
        // File exists - return error (will be handled by caller)
        return -1;
    }
    
    // Create empty file
    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        return -1;  // Failed to create file
    }
    fclose(fp);
    
    // Create metadata
    FileMetadata meta = {0};
    strncpy(meta.owner, owner, sizeof(meta.owner) - 1);
    time_t now = time(NULL);
    meta.created = now;
    meta.last_modified = now;
    meta.last_accessed = now;
    meta.size_bytes = 0;
    meta.word_count = 0;
    meta.char_count = 0;
    
    // Initialize ACL with owner (Step 4)
    meta.acl = acl_init(owner);
    
    // Save metadata
    if (metadata_save(storage_dir, filename, &meta) != 0) {
        // Failed to save metadata - clean up file
        unlink(file_path);
        return -1;
    }
    
    return 0;  // Success
}

// Read entire file content
int file_read(const char *storage_dir, const char *filename,
              char *content_buf, size_t buf_size, size_t *actual_size) {
    if (!storage_dir || !filename || !content_buf || buf_size == 0) return -1;
    
    // Build file path
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/files/%s", storage_dir, filename);
    
    // Open file for reading
    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        return -1;  // File not found or can't open
    }
    
    // Read file content
    size_t read_bytes = fread(content_buf, 1, buf_size - 1, fp);
    fclose(fp);
    
    // Ensure null termination
    content_buf[read_bytes] = '\0';
    
    // Set actual size if pointer provided
    if (actual_size) {
        *actual_size = read_bytes;
    }
    
    return 0;  // Success
}

// Delete file and metadata
int file_delete(const char *storage_dir, const char *filename) {
    if (!storage_dir || !filename) return -1;
    
    // Build paths
    char file_path[512];
    char meta_path[512];
    snprintf(file_path, sizeof(file_path), "%s/files/%s", storage_dir, filename);
    snprintf(meta_path, sizeof(meta_path), "%s/metadata/%s.meta", storage_dir, filename);
    
    // Delete file (unlink returns 0 on success)
    int file_ok = (unlink(file_path) == 0 || errno == ENOENT);
    
    // Delete metadata (unlink returns 0 on success)
    int meta_ok = (unlink(meta_path) == 0 || errno == ENOENT);
    
    // Return success if at least one deletion succeeded (file might not exist)
    // In practice, if file doesn't exist, we should return error
    // But for robustness, we'll accept if metadata is deleted
    if (!file_ok && !meta_ok) {
        return -1;  // Both failed
    }
    
    return 0;  // Success
}

// Check if file exists
int file_exists(const char *storage_dir, const char *filename) {
    if (!storage_dir || !filename) return 0;
    
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/files/%s", storage_dir, filename);
    
    return (access(file_path, F_OK) == 0) ? 1 : 0;
}

// Load metadata from disk
// Metadata format: Simple text format (can be enhanced to JSON later)
// Format: owner=username\ncreated=timestamp\nlast_modified=timestamp\n...
int metadata_load(const char *storage_dir, const char *filename, FileMetadata *metadata) {
    if (!storage_dir || !filename || !metadata) return -1;
    
    // Build metadata file path
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata/%s.meta", storage_dir, filename);
    
    // Open metadata file
    FILE *fp = fopen(meta_path, "r");
    if (!fp) {
        return -1;  // Metadata file not found
    }
    
    // Initialize metadata structure
    memset(metadata, 0, sizeof(FileMetadata));
    
    // Read entire file into buffer for ACL parsing
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *file_content = (char *)malloc(file_size + 1);
    if (!file_content) {
        fclose(fp);
        return -1;
    }
    size_t read_bytes = fread(file_content, 1, file_size, fp);
    if (read_bytes != (size_t)file_size && !feof(fp)) {
        free(file_content);
        fclose(fp);
        return -1;
    }
    file_content[read_bytes] = '\0';
    fclose(fp);
    
    // Parse line by line
    char *saveptr = NULL;
    char *line = strtok_r(file_content, "\n", &saveptr);
    while (line) {
        // Parse key=value pairs
        if (strncmp(line, "owner=", 6) == 0) {
            // Copy owner, truncating if too long (owner field is 64 bytes)
            const char *owner_val = line + 6;
            size_t owner_len = strlen(owner_val);
            if (owner_len >= sizeof(metadata->owner)) {
                owner_len = sizeof(metadata->owner) - 1;
            }
            memcpy(metadata->owner, owner_val, owner_len);
            metadata->owner[owner_len] = '\0';
        } else if (strncmp(line, "created=", 8) == 0) {
            metadata->created = (time_t)atoll(line + 8);
        } else if (strncmp(line, "last_modified=", 14) == 0) {
            metadata->last_modified = (time_t)atoll(line + 14);
        } else if (strncmp(line, "last_accessed=", 14) == 0) {
            metadata->last_accessed = (time_t)atoll(line + 14);
        } else if (strncmp(line, "size_bytes=", 11) == 0) {
            metadata->size_bytes = (size_t)atoll(line + 11);
        } else if (strncmp(line, "word_count=", 11) == 0) {
            metadata->word_count = atoi(line + 11);
        } else if (strncmp(line, "char_count=", 11) == 0) {
            metadata->char_count = atoi(line + 11);
        } else if (strncmp(line, "ACL_START", 9) == 0) {
            // ACL section starts - collect all ACL lines
            char acl_buf[4096] = {0};
            size_t acl_pos = 0;
            
            // Collect ACL lines until ACL_END
            while ((line = strtok_r(NULL, "\n", &saveptr))) {
                if (strncmp(line, "ACL_END", 7) == 0) {
                    break;
                }
                // Append line to ACL buffer
                size_t line_len = strlen(line);
                if (acl_pos + line_len + 1 < sizeof(acl_buf)) {
                    memcpy(acl_buf + acl_pos, line, line_len);
                    acl_pos += line_len;
                    acl_buf[acl_pos++] = '\n';
                }
            }
            acl_buf[acl_pos] = '\0';
            
            // Deserialize ACL
            acl_deserialize(&metadata->acl, acl_buf);
            break;  // ACL is last section
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    
    free(file_content);
    
    // If ACL not found, initialize with owner
    if (metadata->acl.count == 0 && strlen(metadata->owner) > 0) {
        metadata->acl = acl_init(metadata->owner);
    }
    
    return 0;  // Success
}

// Save metadata to disk (atomically)
int metadata_save(const char *storage_dir, const char *filename, const FileMetadata *metadata) {
    if (!storage_dir || !filename || !metadata) return -1;
    
    // Ensure metadata directory exists
    char meta_dir[512];
    snprintf(meta_dir, sizeof(meta_dir), "%s/metadata", storage_dir);
    mkdir(meta_dir, 0755);
    
    // Build paths
    char meta_path[512];
    char tmp_path[520];  // Slightly larger to accommodate ".tmp"
    int n = snprintf(meta_path, sizeof(meta_path), "%s/metadata/%s.meta", storage_dir, filename);
    if (n < 0 || (size_t)n >= sizeof(meta_path)) {
        return -1;  // Path too long
    }
    // Build temp path by appending ".tmp"
    n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", meta_path);
    if (n < 0 || (size_t)n >= sizeof(tmp_path)) {
        return -1;  // Temp path too long
    }
    
    // Write to temporary file first (atomic write)
    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        return -1;
    }
    
    // Write metadata in key=value format
    fprintf(fp, "owner=%s\n", metadata->owner);
    fprintf(fp, "created=%ld\n", (long)metadata->created);
    fprintf(fp, "last_modified=%ld\n", (long)metadata->last_modified);
    fprintf(fp, "last_accessed=%ld\n", (long)metadata->last_accessed);
    fprintf(fp, "size_bytes=%zu\n", metadata->size_bytes);
    fprintf(fp, "word_count=%d\n", metadata->word_count);
    fprintf(fp, "char_count=%d\n", metadata->char_count);
    
    // Write ACL (Step 4)
    fprintf(fp, "ACL_START\n");
    char acl_buf[4096];
    if (acl_serialize(&metadata->acl, acl_buf, sizeof(acl_buf)) == 0) {
        fprintf(fp, "%s", acl_buf);
    }
    fprintf(fp, "ACL_END\n");
    
    fclose(fp);
    
    // Atomically rename temp file to final file
    // This ensures metadata is never corrupted if SS crashes
    if (rename(tmp_path, meta_path) != 0) {
        unlink(tmp_path);  // Clean up temp file
        return -1;
    }
    
    return 0;  // Success
}

// Update last accessed timestamp
int metadata_update_last_accessed(const char *storage_dir, const char *filename) {
    if (!storage_dir || !filename) return -1;
    
    FileMetadata meta;
    if (metadata_load(storage_dir, filename, &meta) != 0) {
        return -1;  // Can't load metadata
    }
    
    meta.last_accessed = time(NULL);
    return metadata_save(storage_dir, filename, &meta);
}

// Update last modified timestamp
int metadata_update_last_modified(const char *storage_dir, const char *filename) {
    if (!storage_dir || !filename) return -1;
    
    FileMetadata meta;
    if (metadata_load(storage_dir, filename, &meta) != 0) {
        return -1;  // Can't load metadata
    }
    
    meta.last_modified = time(NULL);
    
    // Also update file size and word/char counts
    char content[65536];  // Max 64KB for now
    size_t actual_size;
    if (file_read(storage_dir, filename, content, sizeof(content), &actual_size) == 0) {
        meta.size_bytes = actual_size;
        count_file_stats(content, &meta.word_count, &meta.char_count);
    }
    
    return metadata_save(storage_dir, filename, &meta);
}

// Count words and characters in file content
void count_file_stats(const char *content, int *word_count, int *char_count) {
    if (!content) {
        if (word_count) *word_count = 0;
        if (char_count) *char_count = 0;
        return;
    }
    
    int words = 0;
    int chars = 0;
    int in_word = 0;
    
    // Count characters (including spaces)
    for (const char *p = content; *p; p++) {
        chars++;
        
        // Count words: a word is a sequence of non-space characters
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            in_word = 0;  // Not in a word
        } else {
            if (!in_word) {
                words++;  // Start of a new word
                in_word = 1;
            }
        }
    }
    
    // Set results
    if (word_count) *word_count = words;
    if (char_count) *char_count = chars;
}

