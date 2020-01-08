#include <stdio.h>
#include "../headers/cfs.h"

int main(int argc, char const *argv[]) {
    // Check for correct usage
    if (argc <= 1) {
        printf("Usage:cfs_mkdir <DIRECTORIES>\n");
        return 0;
    }
    // Create directories
    int i;
    for (i = 1; i < argc; i++) {
        printf("Creating directory:%s\n",argv[i]);
        // TODO: Implementation
    }
    return 0;
}
