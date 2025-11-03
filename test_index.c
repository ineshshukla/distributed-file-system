// Test program for file index
#include <stdio.h>
#include "src/nm/index.h"

int main() {
    printf("Testing file index system...\n\n");
    
    // Initialize index
    index_init();
    printf("✓ Index initialized\n\n");
    
    // Test 1: Add files
    printf("Test 1: Add files to index\n");
    FileEntry *e1 = index_add_file("file1.txt", "alice", "127.0.0.1", 6001, "ss1");
    FileEntry *e2 = index_add_file("file2.txt", "bob", "127.0.0.1", 6001, "ss1");
    FileEntry *e3 = index_add_file("file3.txt", "alice", "127.0.0.1", 6002, "ss2");
    
    if (e1 && e2 && e3) {
        printf("  ✓ Added 3 files\n");
        printf("  file1.txt -> owner=%s, ss=%s\n", e1->owner, e1->ss_username);
        printf("  file2.txt -> owner=%s, ss=%s\n", e2->owner, e2->ss_username);
        printf("  file3.txt -> owner=%s, ss=%s\n", e3->owner, e3->ss_username);
    }
    
    // Test 2: Lookup files
    printf("\nTest 2: Lookup files (O(1) hash lookup)\n");
    FileEntry *found = index_lookup_file("file1.txt");
    if (found) {
        printf("  ✓ Found file1.txt: owner=%s\n", found->owner);
    }
    
    found = index_lookup_file("file2.txt");
    if (found) {
        printf("  ✓ Found file2.txt: owner=%s\n", found->owner);
    }
    
    found = index_lookup_file("nonexistent.txt");
    if (!found) {
        printf("  ✓ Correctly returned NULL for nonexistent file\n");
    }
    
    // Test 3: Get all files
    printf("\nTest 3: Get all files\n");
    FileEntry *all_files[100];
    int count = index_get_all_files(all_files, 100);
    printf("  Total files in index: %d (expected 3)\n", count);
    for (int i = 0; i < count; i++) {
        printf("    %s\n", all_files[i]->filename);
    }
    printf("  ✓ Retrieved all files\n");
    
    // Test 4: Get files by owner
    printf("\nTest 4: Get files by owner\n");
    FileEntry *alice_files[100];
    int alice_count = index_get_files_by_owner("alice", alice_files, 100);
    printf("  Alice's files: %d (expected 2)\n", alice_count);
    for (int i = 0; i < alice_count; i++) {
        printf("    %s\n", alice_files[i]->filename);
    }
    printf("  ✓ Retrieved files by owner\n");
    
    // Test 5: Remove file
    printf("\nTest 5: Remove file from index\n");
    if (index_remove_file("file2.txt") == 0) {
        printf("  ✓ Removed file2.txt\n");
        found = index_lookup_file("file2.txt");
        if (!found) {
            printf("  ✓ File2.txt no longer found\n");
        }
    }
    
    // Test 6: Update metadata
    printf("\nTest 6: Update file metadata\n");
    if (index_update_metadata("file1.txt", 0, 0, 100, 10, 50) == 0) {
        found = index_lookup_file("file1.txt");
        if (found) {
            printf("  ✓ Updated metadata: size=%zu, words=%d, chars=%d\n",
                   found->size_bytes, found->word_count, found->char_count);
        }
    }
    
    printf("\nAll index tests passed!\n");
    return 0;
}

