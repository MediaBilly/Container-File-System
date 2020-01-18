#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "../headers/cfs.h"
#include "../headers/string_functions.h"

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
    string currentDirectory; // Absolute path for current directory (for cfs_pwd command)
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
    char deleted;
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

int DigitsCount(int num) {
    int digits = 0;
    while (num > 0) {
        digits++;
        num /= 10;
    }
    return digits;
}

int CFS_Init(CFS *cfs) {
    // Initialize cfs structure
    if ((*cfs = malloc(sizeof(struct cfs))) == NULL) {
        // Not enough memory for cfs structure
        return 0;
    }
    // No initial current working file
    memset((*cfs)->currentFile,0,MAX_FILENAME_SIZE);
    (*cfs)->fileDesc = -1;
    (*cfs)->currentDirectory = NULL;
    return 1;
}

unsigned int CFS_GetNextAvailableNodeId(int fileDesc) {
    // Return the id of the 1st hole or last node id + 1 if no holes exist
    // Seek to 1st node metadata
    lseek(fileDesc,sizeof(superblock),SEEK_SET);
    MDS data,tmpData;
    // Continue reading node's metadata until a hole is found or we reach the end of the cfs file
    while (read(fileDesc,&tmpData,sizeof(MDS)) != 0) {
        data = tmpData;
        // Hole found
        if (data.deleted)
            return data.nodeid;
    }
    // End of file reached so new node will be placed there
    return data.nodeid + 1;
}

int CFS_CreateShortcut(int fileDesc,string name,unsigned int nodeid,unsigned int destNodeId) {
    MDS data;
    // Initialize metadata bytes to 0 to avoid valgrind errors
    memset(&data,0,sizeof(MDS));
    // Initialize shortcut metadata
    data.deleted = 0;
    data.nodeid = CFS_GetNextAvailableNodeId(fileDesc);
    strcpy(data.filename,name);
    data.size = DigitsCount(destNodeId);
    data.type = TYPE_SHORTCUT;
    data.parent_nodeid = nodeid;
    time_t timer = time(NULL);
    data.creation_time = data.accessTime = data.modificationTime = timer;
    // Set datablocks data to destination nodeid
    memcpy(data.data.datablocks,&destNodeId,sizeof(unsigned int));
    // Write shortcut data to cfs file
    write(fileDesc,&data,sizeof(MDS));
    // Write directory descriptor to node's list
    lseek(fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    MDS locationData;
    read(fileDesc,&locationData,sizeof(MDS));
    memcpy(locationData.data.datablocks + locationData.size,&data.nodeid,sizeof(int));
    locationData.size += sizeof(int);
    lseek(fileDesc,-sizeof(MDS),SEEK_CUR);
    write(fileDesc,&locationData,sizeof(MDS));
    return 1;
}

int CFS_CreateDirectory(int fileDesc,string name,unsigned int nodeid) {
    MDS data;
    // Initialize metadata bytes to 0 to avoid valgrind errors
    memset(&data,0,sizeof(MDS));
    // Initialize shortcut metadata
    data.deleted = 0;
    data.nodeid = CFS_GetNextAvailableNodeId(fileDesc);
    strcpy(data.filename,name);
    data.size = 0;
    data.type = TYPE_DIRECTORY;
    data.parent_nodeid = nodeid;
    time_t timer = time(NULL);
    data.creation_time = data.accessTime = data.modificationTime = timer;
    // Write directory data to cfs file
    write(fileDesc,&data,sizeof(MDS));
    // Write directory descriptor to node's list
    lseek(fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    MDS locationData;
    read(fileDesc,&locationData,sizeof(MDS));
    memcpy(locationData.data.datablocks + locationData.size,&data.nodeid,sizeof(int));
    locationData.size += sizeof(int);
    lseek(fileDesc,-sizeof(MDS),SEEK_CUR);
    write(fileDesc,&locationData,sizeof(MDS));
    // Create . shortcut
    CFS_CreateShortcut(fileDesc,".",data.nodeid,data.nodeid);
    // Create .. shortcut
    CFS_CreateShortcut(fileDesc,"..",data.nodeid,nodeid);
    return 1;
}

int Create_CFS_File(string pathname,int BLOCK_SIZE,int FILENAME_SIZE,int MAX_FILE_SIZE,int MAX_DIRECTORY_FILE_NUMBER) {
    // Create the file
    int fd = open(pathname,O_RDWR|O_CREAT|O_TRUNC,FILE_PERMISSIONS);
    // Check if creation was successful
    if (fd != -1) {
        // Write superblock data
        superblock sb = {BLOCK_SIZE, FILENAME_SIZE, MAX_FILE_SIZE, MAX_DIRECTORY_FILE_NUMBER};
        lseek(fd,0,SEEK_END);
        write(fd,&sb,sizeof(superblock));
        // Write root node data
        MDS data;
        // Initialize metadata bytes to 0 to avoid valgrind errors
        memset(&data,0,sizeof(MDS));
        // Initialize root directory metadata
        data.deleted = 0;
        data.nodeid = 0;
        strcpy(data.filename,"/");
        data.size = 0;
        data.type = TYPE_DIRECTORY;
        data.parent_nodeid = NO_PARENT;
        time_t timer = time(NULL);
        data.creation_time = data.accessTime = data.modificationTime = timer;
        lseek(fd,0,SEEK_END);
        write(fd,&data,sizeof(MDS));
        // Create . shortcut
        CFS_CreateShortcut(fd,".",0,0);
        // Create .. shortcut
        CFS_CreateShortcut(fd,"..",0,0);
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
                // Close previous open cfs file if exists
                if (cfs->fileDesc != -1) {
                    close(cfs->fileDesc);
                }
                // Read filename
                string file = readNextWord(&lastword);
                // Check if file exists
                if ((cfs->fileDesc = open(file,O_RDWR,FILE_PERMISSIONS)) < 0) {
                    printf("File %s does not exist\n",file);
                } else {
                    // File exists so open it
                    strcpy(cfs->currentFile,file);
                    // Set current directory to root (/)
                    cfs->currentDirectoryId = 0;
                    cfs->currentDirectory = (string)malloc(2*sizeof(char));
                    strcpy(cfs->currentDirectory,"/");
                }
                DestroyString(&file);
            } else {
                // File not specified
                printf("Usage:cfs_workwith <FILE>\n");
            }
        }
        // Create directory (or directories)
        else if (!strcmp("cfs_mkdir",commandLabel)) {
            if (cfs->fileDesc != -1) {
                // Check if directories were specified
                if (!lastword) {
                    string dir;
                    while (!lastword) {
                        dir = readNextWord(&lastword);
                        CFS_CreateDirectory(cfs->fileDesc,dir,cfs->currentDirectoryId);
                        DestroyString(&dir);
                    }
                    DestroyString(&dir);
                } else {
                    printf("Usage:cfs_mkdir <DIRECTORIES>\n");
                }
            } else {
                printf("Not currently working with a cfs file.\n");
                if (!lastword)
                    IgnoreRemainingInput();
            }
        }
        // Print working directory (absolute path)
        else if (!strcmp("cfs_pwd",commandLabel)) {
            if (cfs->fileDesc != -1) {
                printf("%s\n",cfs->currentDirectory);
            } else {
                printf("Not currently working with a cfsd file.\n");
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
                    DestroyString(&option);
                    DestroyString(&option_argument);
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
                DestroyString(&option);
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
        DestroyString(&commandLabel);
    }
    return 1;
}

int CFS_Destroy(CFS *cfs) {
    if (*cfs != NULL) {
        // Close open cfs file if exists
        if ((*cfs)->fileDesc != -1) {
            close((*cfs)->fileDesc);
        }
        // Free allocated memory for cfs
        if ((*cfs)->currentDirectory != NULL)
            free((*cfs)->currentDirectory);
        free(*cfs);
        *cfs = NULL;
        return 1;
    }
    // Not yet initialize
    return 0;
}
