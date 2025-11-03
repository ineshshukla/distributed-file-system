#ifndef FILE_STORAGE_H
#define FILE_STORAGE_H

#include <stddef.h>
#include <time.h>

// File storage module for Storage Server
// Handles all file operations: create, read, delete, metadata management
// Ensures data persistence across SS restarts

// Storage directory structure:
//   storage_dir/
//     ├── files/          # Actual file content
//     │   ├── file1.txt
//     │   └── file2.txt
//     └── metadata/       # File metadata (owner, timestamps, ACL, etc.)
//         ├── file1.txt.meta
//         └── file2.txt.meta

// File metadata structure - stores all information about a file
typedef struct {
    char owner[64];           // Username of file owner
    time_t created;           // Creation timestamp
    time_t last_modified;     // Last modification timestamp
    time_t last_accessed;     // Last access timestamp
    size_t size_bytes;        // File size in bytes
    int word_count;           // Total word count (for INFO command)
    int char_count;           // Total character count (for INFO command)
    // Note: ACL will be added in later phase (Step 4)
} FileMetadata;

// Create an empty file in the storage directory
// storage_dir: Base storage directory (e.g., "./storage_ss1")
// filename: Name of the file to create
// owner: Username of the file owner
// Returns: 0 on success, -1 on error
//
// This function:
// 1. Creates the files/ and metadata/ directories if they don't exist
// 2. Creates an empty file in files/ directory
// 3. Creates metadata file with owner and timestamps
// 4. Initializes metadata with current time
//
// Usage:
//   if (file_create("./storage_ss1", "test.txt", "alice") == 0) {
//       printf("File created successfully\n");
//   }
int file_create(const char *storage_dir, const char *filename, const char *owner);

// Read the entire content of a file
// storage_dir: Base storage directory
// filename: Name of the file to read
// content_buf: Buffer to store file content (caller allocates)
// buf_size: Size of the buffer
// actual_size: Pointer to store actual file size (can be NULL)
// Returns: 0 on success, -1 on error
//
// Note: This reads the entire file into memory. For large files,
// a streaming approach would be better, but for Phase 2 this is sufficient.
//
// Usage:
//   char content[4096];
//   size_t actual_size;
//   if (file_read("./storage_ss1", "test.txt", content, sizeof(content), &actual_size) == 0) {
//       printf("File content: %.*s\n", (int)actual_size, content);
//   }
int file_read(const char *storage_dir, const char *filename, 
              char *content_buf, size_t buf_size, size_t *actual_size);

// Delete a file and its metadata
// storage_dir: Base storage directory
// filename: Name of the file to delete
// Returns: 0 on success, -1 on error (file not found, permission denied, etc.)
//
// This function:
// 1. Deletes the file from files/ directory
// 2. Deletes the metadata file from metadata/ directory
// 3. Returns error if file doesn't exist
//
// Usage:
//   if (file_delete("./storage_ss1", "test.txt") == 0) {
//       printf("File deleted successfully\n");
//   }
int file_delete(const char *storage_dir, const char *filename);

// Check if a file exists in the storage directory
// storage_dir: Base storage directory
// filename: Name of the file to check
// Returns: 1 if file exists, 0 if not
//
// Usage:
//   if (file_exists("./storage_ss1", "test.txt")) {
//       printf("File exists\n");
//   }
int file_exists(const char *storage_dir, const char *filename);

// Load metadata from disk
// storage_dir: Base storage directory
// filename: Name of the file
// metadata: Pointer to FileMetadata structure to populate
// Returns: 0 on success, -1 on error (metadata file not found, parse error, etc.)
//
// This reads the metadata file (metadata/filename.meta) and parses it
// into the FileMetadata structure.
//
// Usage:
//   FileMetadata meta;
//   if (metadata_load("./storage_ss1", "test.txt", &meta) == 0) {
//       printf("Owner: %s\n", meta.owner);
//   }
int metadata_load(const char *storage_dir, const char *filename, FileMetadata *metadata);

// Save metadata to disk
// storage_dir: Base storage directory
// filename: Name of the file
// metadata: Pointer to FileMetadata structure to save
// Returns: 0 on success, -1 on error
//
// This writes the metadata to disk atomically:
// 1. Write to temporary file (metadata/filename.meta.tmp)
// 2. Rename to final file (metadata/filename.meta)
// This ensures metadata is never corrupted if SS crashes during write.
//
// Usage:
//   FileMetadata meta = {0};
//   strcpy(meta.owner, "alice");
//   meta.created = time(NULL);
//   if (metadata_save("./storage_ss1", "test.txt", &meta) == 0) {
//       printf("Metadata saved\n");
//   }
int metadata_save(const char *storage_dir, const char *filename, const FileMetadata *metadata);

// Update last accessed timestamp in metadata
// This is called whenever a file is read (for INFO command)
// storage_dir: Base storage directory
// filename: Name of the file
// Returns: 0 on success, -1 on error
//
// Usage:
//   metadata_update_last_accessed("./storage_ss1", "test.txt");
int metadata_update_last_accessed(const char *storage_dir, const char *filename);

// Update last modified timestamp in metadata
// This is called whenever a file is written (for future WRITE command)
// storage_dir: Base storage directory
// filename: Name of the file
// Returns: 0 on success, -1 on error
//
// Usage:
//   metadata_update_last_modified("./storage_ss1", "test.txt");
int metadata_update_last_modified(const char *storage_dir, const char *filename);

// Count words and characters in file content
// This is used for INFO command to display statistics
// content: File content (null-terminated string)
// word_count: Pointer to store word count (can be NULL)
// char_count: Pointer to store character count (can be NULL)
//
// Word definition: sequence of ASCII characters without spaces
// Character count: total number of characters (including spaces)
//
// Usage:
//   int words, chars;
//   count_file_stats("Hello world!", &words, &chars);
//   // words = 2, chars = 12
void count_file_stats(const char *content, int *word_count, int *char_count);

#endif

