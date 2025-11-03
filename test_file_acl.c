// Test file storage with ACL integration
#include <stdio.h>
#include "src/ss/file_storage.h"

int main() {
    printf("Testing file storage with ACL...\n\n");
    
    const char *storage_dir = "./storage_ss1";
    const char *filename = "acl_test.txt";
    const char *owner = "alice";
    
    // Test 1: Create file (should initialize ACL)
    printf("Test 1: Create file with ACL\n");
    if (file_create(storage_dir, filename, owner) == 0) {
        printf("  ✓ File created\n");
    }
    
    // Test 2: Load metadata and check ACL
    printf("\nTest 2: Load metadata and verify ACL\n");
    FileMetadata meta;
    if (metadata_load(storage_dir, filename, &meta) == 0) {
        printf("  Owner: %s\n", meta.owner);
        printf("  ACL owner: %s\n", meta.acl.owner);
        printf("  ACL entries: %d\n", meta.acl.count);
        printf("  ✓ Metadata loaded with ACL\n");
        
        // Test ACL functions
        printf("\n  ACL checks:\n");
        printf("    Alice (owner) read: %d (expected 1)\n", acl_check_read(&meta.acl, "alice"));
        printf("    Alice (owner) write: %d (expected 1)\n", acl_check_write(&meta.acl, "alice"));
        printf("    Bob (no access) read: %d (expected 0)\n", acl_check_read(&meta.acl, "bob"));
        printf("    ✓ ACL initialized correctly\n");
    }
    
    // Test 3: Modify ACL and save
    printf("\nTest 3: Modify ACL and save\n");
    if (metadata_load(storage_dir, filename, &meta) == 0) {
        // Add read access to bob
        acl_add_read(&meta.acl, "bob");
        printf("  ✓ Added read access to bob\n");
        
        // Save metadata
        if (metadata_save(storage_dir, filename, &meta) == 0) {
            printf("  ✓ Metadata saved\n");
        }
        
        // Reload and verify
        FileMetadata meta2;
        if (metadata_load(storage_dir, filename, &meta2) == 0) {
            printf("  ACL entries after reload: %d (expected 1)\n", meta2.acl.count);
            printf("  Bob read access: %d (expected 1)\n", acl_check_read(&meta2.acl, "bob"));
            printf("  ✓ ACL persisted correctly\n");
        }
    }
    
    printf("\nAll file storage ACL tests passed!\n");
    return 0;
}

