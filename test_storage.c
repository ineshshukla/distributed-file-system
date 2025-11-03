// Quick test program for file storage functions
#include <stdio.h>
#include "src/ss/file_storage.h"

int main() {
    const char *storage_dir = "./storage_ss1";
    const char *filename = "test_storage.txt";
    const char *owner = "testuser";
    
    printf("Testing file_create...\n");
    if (file_create(storage_dir, filename, owner) == 0) {
        printf("✓ File created successfully\n");
    } else {
        printf("✗ File creation failed\n");
        return 1;
    }
    
    printf("Testing file_exists...\n");
    if (file_exists(storage_dir, filename)) {
        printf("✓ File exists\n");
    } else {
        printf("✗ File does not exist\n");
        return 1;
    }
    
    printf("Testing metadata_load...\n");
    FileMetadata meta;
    if (metadata_load(storage_dir, filename, &meta) == 0) {
        printf("✓ Metadata loaded\n");
        printf("  Owner: %s\n", meta.owner);
        printf("  Size: %zu bytes\n", meta.size_bytes);
    } else {
        printf("✗ Metadata load failed\n");
        return 1;
    }
    
    printf("Testing file_read...\n");
    char content[1024];
    size_t actual_size;
    if (file_read(storage_dir, filename, content, sizeof(content), &actual_size) == 0) {
        printf("✓ File read successfully (%zu bytes)\n", actual_size);
        printf("  Content: '%s'\n", content);
    } else {
        printf("✗ File read failed\n");
        return 1;
    }
    
    printf("Testing metadata_update_last_accessed...\n");
    if (metadata_update_last_accessed(storage_dir, filename) == 0) {
        printf("✓ Last accessed updated\n");
    } else {
        printf("✗ Update failed\n");
        return 1;
    }
    
    printf("All tests passed!\n");
    return 0;
}

