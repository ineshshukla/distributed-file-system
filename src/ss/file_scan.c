#include "file_scan.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Scan the storage directory for existing files
// This is called during SS startup to discover what files already exist
ScanResult scan_directory(const char *storage_dir, const char *files_dir) {
    ScanResult result = {0};  // Initialize with count = 0
    
    // Build full path to files directory: storage_dir/files_dir
    // Example: "./storage_ss1/files"
    char files_path[512];
    snprintf(files_path, sizeof(files_path), "%s/%s", storage_dir, files_dir);
    
    // Open the directory for reading
    DIR *dir = opendir(files_path);
    if (!dir) {
        // Directory doesn't exist yet - that's OK, just return empty result
        // The directory will be created when first file is created
        return result;
    }
    
    // Read each entry in the directory
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && result.count < MAX_FILES_PER_SS) {
        // Skip special entries: "." (current directory) and ".." (parent directory)
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Build full path to this file
        // Note: files_path can be up to 512, entry->d_name up to 255, so we need 768
        char file_path[768];
        int n = snprintf(file_path, sizeof(file_path), "%s/%s", files_path, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(file_path)) {
            // Path too long - skip this file
            continue;
        }
        
        // Get file information using stat()
        struct stat st;
        if (stat(file_path, &st) != 0) {
            // Can't stat this file - skip it
            continue;
        }
        
        // Only process regular files (not directories or symlinks)
        if (!S_ISREG(st.st_mode)) {
            continue;
        }
        
        // Store file information in result
        ScannedFile *file = &result.files[result.count];
        (void)snprintf(file->filename, sizeof(file->filename), "%s", entry->d_name);
        file->size_bytes = (size_t)st.st_size;
        
        // Check if metadata file exists
        // Metadata files are stored in: storage_dir/metadata/filename.meta
        char meta_path[512];
        snprintf(meta_path, sizeof(meta_path), "%s/metadata/%s.meta", storage_dir, entry->d_name);
        file->has_metadata = (access(meta_path, F_OK) == 0) ? 1 : 0;
        
        result.count++;
    }
    
    closedir(dir);
    return result;
}

// Build a comma-separated file list string from scan results
// This string is sent to NM in the SS_REGISTER payload
int build_file_list_string(const ScanResult *result, char *buf, size_t buflen) {
    if (!result || !buf || buflen == 0) return -1;
    
    // Start with empty string
    buf[0] = '\0';
    size_t pos = 0;
    
    // If no files, return empty string (that's OK)
    if (result->count == 0) {
        return 0;
    }
    
    // Build comma-separated list: "file1.txt,file2.txt,file3.txt"
    for (int i = 0; i < result->count; i++) {
        const char *filename = result->files[i].filename;
        size_t filename_len = strlen(filename);
        
        // Check if we have space (need space for filename + comma + null terminator)
        // If this is the last file, no comma needed
        size_t needed = filename_len + ((i < result->count - 1) ? 1 : 0);
        if (pos + needed + 1 >= buflen) {
            // Buffer too small - truncate
            return -1;
        }
        
        // Append filename
        strcpy(buf + pos, filename);
        pos += filename_len;
        
        // Append comma if not last file
        if (i < result->count - 1) {
            buf[pos++] = ',';
        }
    }
    
    buf[pos] = '\0';  // Ensure null termination
    return 0;
}

