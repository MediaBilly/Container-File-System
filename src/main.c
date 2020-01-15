#include <stdio.h>
#include "../headers/cfs.h"

int main(int argc, char const *argv[]) {
    // Check for correct usage
    if (argc != 1) {
        printf("Usage:./cfs\n");
        return 0;
    }
    // Initialize cfs structure
    CFS cfs;
    if (!CFS_Init(&cfs)) {
        printf("Not enough memory for cfs structure\n");
        return 0;
    }
    if (!CFS_Run(cfs)) {
        printf("CFS structure not yet initialized\n");
        return 0;
    }
    // Destroy cfs structure after usage
    CFS_Destroy(&cfs);
    return 0;
}
