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

struct cfs {
    int fileDesc;
    char currentFile[MAX_FILENAME_SIZE];
};

typedef struct {
    int BLOCK_SIZE;
    int FILENAME_SIZE;
    int MAX_FILE_SIZE;
    int MAX_DIRECTORY_FILE_NUMBER;
} superblock;

typedef struct {
    unsigned int datablocks[DATABLOCK_NUM];
} Datastream;

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
        strcpy(pathname,data.filename);
        data.size = 0;
        data.type = TYPE_DIRECTORY;
        data.parent_nodeid = NO_PARENT;
        time_t timer = time(NULL);
        data.creation_time = data.accessTime = data.modificationTime = timer;
        write(fd,&data,sizeof(MDS));
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
        commandLabel = readNextWord();
        // Check if it was specified
        if (strlen(commandLabel)) {
            // Work with specific file
            if (!strcmp("cfs_workwith",commandLabel)) {
                // Read filename
                string file;
                // Check if it was specified
                if ((file = readNextWord()) != NULL) {
                    // Check if file exists
                    if ((cfs->fileDesc = open(file,O_RDWR,FILE_PERMISSIONS)) < 0) {
                        printf("File %s does not exist\n",file);
                    } else {
                        // File exists so check if it is cfs format

                        strcpy(cfs->currentFile,file);
                    }
                } else {
                    printf("Usage:cfs_workwith <FILE>\n");
                }
            }
            // Create new cfs file
            else if (!strcmp("cfs_create",commandLabel)) {
                // Read options
                string option,option_argument;
                // Read first option or file
                option = readNextWord();
                int ok = 1;
                int BLOCK_SIZE = sizeof(unsigned int),FILENAME_SIZE = MAX_FILENAME_SIZE,MAX_FILE_SIZE = DATABLOCK_NUM,MAX_DIRECTORY_FILE_NUMBER = 50;
                while (option != NULL && option[0] == '-') {
                    // Read option argument
                    if ((option_argument = readNextWord()) != NULL) {
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
                    }
                    // Read new option
                    option = readNextWord();
                }
                // No wrong usage of any command so continue on file creation
                if (ok) {
                    // Last word is the file
                    string file = option;
                    // Create the file
                    Create_CFS_File(file,BLOCK_SIZE,FILENAME_SIZE,MAX_FILE_SIZE,MAX_DIRECTORY_FILE_NUMBER);
                }
            } else {
                printf("Wrong command.\n");
            }
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
