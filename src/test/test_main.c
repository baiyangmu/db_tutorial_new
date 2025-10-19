#include <stdio.h>
#include <stdlib.h>

/* Test runners */
extern int test_schema_main(void);
extern int test_util_main(void);

int main(int argc, char* argv[]) {
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   MyDB Unit Test Suite                ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    
    int failed = 0;
    
    /* Run schema tests */
    printf("➤ Running Schema Tests...\n");
    system("./test_schema");
    
    /* Run util tests */
    printf("➤ Running Util Tests...\n");
    system("./test_util");
    
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   Test Summary                         ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    printf("  All unit tests completed!\n");
    printf("  Check output above for any failures.\n");
    printf("\n");
    
    return 0;
}

