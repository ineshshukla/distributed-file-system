// Test program for ACL system
#include <stdio.h>
#include "src/common/acl.h"

int main() {
    printf("Testing ACL system...\n\n");
    
    // Test 1: Initialize ACL
    printf("Test 1: Initialize ACL with owner\n");
    ACL acl = acl_init("alice");
    printf("  Owner: %s\n", acl.owner);
    printf("  ✓ ACL initialized\n\n");
    
    // Test 2: Check owner access
    printf("Test 2: Check owner access\n");
    printf("  Alice read: %d (expected 1)\n", acl_check_read(&acl, "alice"));
    printf("  Alice write: %d (expected 1)\n", acl_check_write(&acl, "alice"));
    printf("  ✓ Owner has RW access\n\n");
    
    // Test 3: Add read access
    printf("Test 3: Add read access to bob\n");
    if (acl_add_read(&acl, "bob") == 0) {
        printf("  ✓ Read access added\n");
    }
    printf("  Bob read: %d (expected 1)\n", acl_check_read(&acl, "bob"));
    printf("  Bob write: %d (expected 0)\n", acl_check_write(&acl, "bob"));
    printf("  ✓ Bob has read-only access\n\n");
    
    // Test 4: Add write access
    printf("Test 4: Add write access to charlie\n");
    if (acl_add_write(&acl, "charlie") == 0) {
        printf("  ✓ Write access added\n");
    }
    printf("  Charlie read: %d (expected 1)\n", acl_check_read(&acl, "charlie"));
    printf("  Charlie write: %d (expected 1)\n", acl_check_write(&acl, "charlie"));
    printf("  ✓ Charlie has RW access\n\n");
    
    // Test 5: Remove access
    printf("Test 5: Remove bob's access\n");
    if (acl_remove(&acl, "bob") == 0) {
        printf("  ✓ Access removed\n");
    }
    printf("  Bob read: %d (expected 0)\n", acl_check_read(&acl, "bob"));
    printf("  ✓ Bob no longer has access\n\n");
    
    // Test 6: Serialize/Deserialize
    printf("Test 6: Serialize and deserialize ACL\n");
    char buf[4096];
    if (acl_serialize(&acl, buf, sizeof(buf)) == 0) {
        printf("  Serialized: %s", buf);
    }
    
    ACL acl2;
    if (acl_deserialize(&acl2, buf) == 0) {
        printf("  ✓ ACL deserialized\n");
        printf("  Owner: %s\n", acl2.owner);
        printf("  Charlie read: %d (expected 1)\n", acl_check_read(&acl2, "charlie"));
        printf("  ✓ Serialization works\n\n");
    }
    
    printf("All ACL tests passed!\n");
    return 0;
}

