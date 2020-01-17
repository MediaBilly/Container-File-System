#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "../headers/cfs.h"
#include "../headers/utilities.h"

// File definitions
#define DATABLOCK_NUM 1000
#define FILE_PERMISSIONS 0644
#define MAX_FILENAME_SIZE 50

// Define file types
#define TYPE_FILE 0
#define TYPE_DIRECTORY 1
#define TYPE_SHORTCUT 2

// Other definitions
#define NO_PARENT -1

// CFS structure definition
struct cfs {
    int fileDesc; // File descriptor of currently working cfs file
    char currentFile[MAX_FILENAME_SIZE]; // Name of currently working cfs file
    int currentDirectoryId; // Nodeid for current directory
};

// Superblock definition
typedef struct {
    int BLOCK_SIZE;
    int FILENAME_SIZE;
    int MAX_FILE_SIZE;
    int MAX_DIRECTORY_FILE_NUMBER;
} superblock;

// Datastream definition:
// If entity is directory datablocks are the id's of the entities that the dir contains
// If entity is shortcut datablocks contain the id of the entity that the shortcut connects to
// If entity is file datablocks contain the file contents
typedef struct {
    char datablocks[DATABLOCK_NUM];
} Datastream;

// Metadata structure definition
typedef struct {
    unsigned int nodeid;
    char filename[MAX_FILENAME_SIZE];
    unsigned int size;
    unsigned int type;
    unsigned int parent_nodeid;
    time_t creation_time;
    time_t accessTime;
    time_t modificationTime;
    Datastream data;
} MDS;

int CFS_Init(CFS *cfs) {
    // Initialize cfs structure
    if ((*cfs = malloc(sizeof(struct cfs))) == NULL) {
        // Not enough memory for cfs structure
        return 0;
    }
    // No initial current working file
    memset((*cfs)->currentFile,0,MAX_FILENAME_SIZE);
    (*cfs)->fileDesc = -1;
    return 1;
}

int Create_CFS_File(string pathname,int BLOCK_SIZE,int FILENAME_SIZE,int MAX_FILE_SIZE,int MAX_DIRECTORY_FILE_NUMBER) {
    // Create the file
    int fd = creat(pathname,FILE_PERMISSIONS);
    // Check if creation was successful
    if (fd != -1) {
        // Write superblock data
        superblock sb = {BLOCK_SIZE, FILENAME_SIZE, MAX_FILE_SIZE, MAX_DIRECTORY_FILE_NUMBER};
        write(fd,&sb,sizeof(superblock));
        // Write root node data
        MDS data;
        data.nodeid = 0;
        strcpy(data.filename,pathname);
        data.size = 0;
        data.type = TYPE_DIRECTORY;
        data.parent_nodeid = NO_PARENT;
        time_t timer = time(NULL);
        data.creation_time = data.accessTime = data.modificationTime = timer;
        // Set datablocks to empty content
        data.data.datablocks[0] = '\0';
        write(fd,&data,sizeof(MDS));
        // Close the file after writing data
        close(fd);
    } else {
        perror("Error creating cfs file:");
        return -1;
    }
    return fd;
}

int CFS_Run(CFS cfs) {
    int running = 1;
    char *commandLabel;
    // CFS terminal
    while (running) {
        printf("%s>",cfs->currentFile);
        // Read command label
        int lastword;
        commandLabel = readNextWord(&lastword);
        // Work with specific file
        if (!strcmp("cfs_workwith",commandLabel)) {
            // Check if it was specified
            if (!lastword) {
                // Read filename
                string file = readNextWord(&lastword);
                // Check if file exists
                if ((cfs->fileDesc = open(file,O_RDWR,FILE_PERMISSIONS)) < 0) {
                    printf("File %s does not exist\n",file);
                } else {
                    // File exists so open it
                    strcpy(cfs->currentFile,file);
                    cfs->fileDesc = open(file,O_RDWR);
                    // Set current directory to root (/)
                    cfs->currentDirectoryId = 0;
                }
            } else {
                // File not specified
                printf("Usage:cfs_workwith <FILE>\n");
            }
        }
        // Create new cfs file
        else if (!strcmp("cfs_create",commandLabel)) {
            // Check if options were specified
            if (!lastword) {
                // Read options
                string option,option_argument;
                // Read first option or file
                option = readNextWord(&lastword);
                int ok = 1;
                int BLOCK_SIZE = sizeof(char),FILENAME_SIZE = MAX_FILENAME_SIZE,MAX_FILE_SIZE = DATABLOCK_NUM,MAX_DIRECTORY_FILE_NUMBER = 50;
                while (option[0] == '-') {
                    // Check if option argument was not specified
                    if (lastword) {
                        ok = 0;
                        break;
                    }
                    // Read option argument
                    option_argument = readNextWord(&lastword);
                    if (!lastword) {
                        // Handle each option flag
                        if (!strcmp("-bs",option)) {
                            // BLOCK_SIZE
                            BLOCK_SIZE = atoi(option_argument);
                        }
                        else if (!strcmp("-fns",option)) {
                            // FILENAME_SIZE
                            FILENAME_SIZE = atoi(option_argument);
                        }
                        else if (!strcmp("-cfs",option)) {
                            // MAX_FILE_SIZE
                            MAX_FILE_SIZE = atoi(option_argument);
                        }
                        else if (!strcmp("-mdfn",option)) {
                            // MAX_DIRECTORY_FILE_NUMBER
                            MAX_DIRECTORY_FILE_NUMBER = atoi(option_argument);
                        }
                        else {
                            printf("Wrong option\n");
                            ok = 0;
                            break;
                        }
                    } else {
                        ok = 0;
                        break;
                    }
                    // Read new option
                    option = readNextWord(&lastword);
                }
                // No wrong usage of any command so continue on file creation
                if (ok) {
                    // Last word is the file
                    string file = option;
                    // Create the file
                    Create_CFS_File(file,BLOCK_SIZE,FILENAME_SIZE,MAX_FILE_SIZE,MAX_DIRECTORY_FILE_NUMBER);
                } else {
                    // No file specified
                    printf("Usage:cfs_workwith <OPTIONS> <FILE>\n");
                }
            } else {
                // No file specified
                printf("Usage:cfs_workwith <OPTIONS> <FILE>\n");
            }
        }
        // Exit cfs interface 
        else if (!strcmp("cfs_exit",commandLabel)) {
            running = 0;
        } else {
            printf("Wrong command.\n");
        }
    }
    return 1;
}

int CFS_Destroy(CFS *cfs) {
    if (*cfs != NULL) {
        // Free allocated memory for cfs
        free(*cfs);
        *cfs = NULL;
        return 1;
    }
    // Not yet initialize
    return 0;
}
