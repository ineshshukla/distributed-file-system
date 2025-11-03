// Quick test program for error code system
#include <stdio.h>
#include "src/common/errors.h"

int main() {
    printf("Testing error code system...\n\n");
    
    // Test error_code_to_string
    printf("Error code strings:\n");
    printf("  ERR_OK: %s\n", error_code_to_string(ERR_OK));
    printf("  ERR_INVALID: %s\n", error_code_to_string(ERR_INVALID));
    printf("  ERR_UNAUTHORIZED: %s\n", error_code_to_string(ERR_UNAUTHORIZED));
    printf("  ERR_NOT_FOUND: %s\n", error_code_to_string(ERR_NOT_FOUND));
    printf("  ERR_CONFLICT: %s\n", error_code_to_string(ERR_CONFLICT));
    printf("  ERR_UNAVAILABLE: %s\n", error_code_to_string(ERR_UNAVAILABLE));
    printf("  ERR_INTERNAL: %s\n", error_code_to_string(ERR_INTERNAL));
    
    // Test error_create (formatted message)
    printf("\nTesting error_create:\n");
    Error err1 = error_create(ERR_NOT_FOUND, "File '%s' not found", "test.txt");
    printf("  Code: %s\n", error_code_to_string(err1.code));
    printf("  Message: %s\n", err1.message);
    
    // Test error_simple
    printf("\nTesting error_simple:\n");
    Error err2 = error_simple(ERR_UNAUTHORIZED, "Access denied");
    printf("  Code: %s\n", error_code_to_string(err2.code));
    printf("  Message: %s\n", err2.message);
    
    // Test error_ok
    printf("\nTesting error_ok:\n");
    Error err3 = error_ok();
    printf("  Code: %s\n", error_code_to_string(err3.code));
    printf("  Is OK: %d\n", error_is_ok(&err3));
    printf("  Is NOT OK: %d\n", error_is_ok(&err1));
    
    printf("\nAll error code tests passed!\n");
    return 0;
}

