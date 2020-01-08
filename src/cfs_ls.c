#include <stdio.h>
#include <string.h>
#include "../headers/cfs.h"

#define ALL_FILES 0
#define RECURSIVE_PRINT 1
#define ALL_ATTRIBUTES 2
#define UNORDERED 3
#define DIRECTORIES_ONLY 4
#define LINKS_ONLY 5

int main(int argc, char const *argv[]) {
    // For each file read it's options and it's name
    int i;
    int options[6] = {0,0,0,0,0,0};
    int files = 0;
    for (i = 1; i < argc;) {
        // Read options
        while (i < argc && (!strcmp("-a",argv[i]) || !strcmp("-r",argv[i]) || !strcmp("-l",argv[i]) || !strcmp("-u",argv[i]) || !strcmp("-d",argv[i]) || !strcmp("-h",argv[i]))) {
            // All files option
            if (!strcmp("-a",argv[i])) {
                options[ALL_FILES] = 1;
            }
            else if (!strcmp("-r",argv[i])) {
                options[RECURSIVE_PRINT] = 1;
            }
            else if (!strcmp("-l",argv[i])) {
                options[ALL_ATTRIBUTES] = 1;
            }
            else if (!strcmp("-u",argv[i])) {
                options[UNORDERED] = 1;
            }
            else if (!strcmp("-d",argv[i])) {
                options[DIRECTORIES_ONLY] = 1;
            }
            else if (!strcmp("-h",argv[i])) {
                options[LINKS_ONLY] = 1;
            }
            i++;
        }
        // No options so activate all attributes option by default
        if (!(options[ALL_FILES] || options[RECURSIVE_PRINT] || options[ALL_ATTRIBUTES] || options[UNORDERED] || options[DIRECTORIES_ONLY] || options[LINKS_ONLY])) {
            options[ALL_ATTRIBUTES] = 1;
        }
        // Read files
        for(;i < argc;i++) {
            printf("Showing attributes of file:%s\n",argv[i]);
            files++;
            // TODO: Implementation
        }
    }
    // No files so show them all
    if (files == 0) {
        printf("Showing all attributes of all files.\n");
        // TODO: Implementation
    }
    return 0;
}
