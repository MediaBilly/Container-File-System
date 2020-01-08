#include <stdio.h>
#include <string.h>
#include "../headers/cfs.h"

#define ACCESS 0
#define MODIFICATION 1

int main(int argc, char const *argv[]) {
    // Check for correct usage
    if (argc <= 1) {
        printf("Usage:cfs_touch <OPTIONS> <FILES>\n");
        return 0;
    }
    // For each file read it's options and it's name
    int i;
    int options[2] = {0,0};
    for (i = 1; i < argc;) {
        // Read options
        while (!strcmp("-a",argv[i]) || !strcmp("-m",argv[i])) {
            // Modify access time only option
            if (!strcmp("-a",argv[i])) {
                options[ACCESS] = 1;
            }
            else if (!strcmp("-m",argv[i])) {
                options[MODIFICATION] = 1;
            }
            i++;
        }
        // No options so activate both by default
        if (!(options[ACCESS] || options[MODIFICATION])) {
            options[ACCESS] = options[MODIFICATION] = 1;
        }
        // Read files
        for(;i < argc;i++) {
            // Both options
            if (options[ACCESS] && options[MODIFICATION]) {
                printf("Creating or modifying both timestamps in file:%s\n",argv[i]);
                // TODO: Implementation
            }
            // Access time only
            else if (options[ACCESS]) {
                printf("Creating or modifying access timestamp only in file:%s\n",argv[i]);
                // TODO: Implementation
            }
            // Modification time only
            else if (options[MODIFICATION]) {
                printf("Creating or modifying modification timestamp only in file:%s\n",argv[i]);
                // TODO: Implementation
            }
        }
    }
    return 0;
}
