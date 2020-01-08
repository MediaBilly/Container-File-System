#include <stdio.h>
#include "../headers/cfs.h"

int main(int argc, char const *argv[]) {
    if (argc <= 2) {
        printf("Usage:cfs_cp <OPTIONS> <SOURCE> <DESTINATION> | <OPTIONS> <SOURCES> ... <DIRECTORY>\n");
        return 0;
    }
    return 0;
}
