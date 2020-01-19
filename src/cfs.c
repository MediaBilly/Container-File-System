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

// Define option flags
#define TOUCH_ACCESS 0
#define TOUCH_MODIFICATION 1

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
    char deleted; // 1 if the entity was previously deleted and o otherwise
    char root; // 1 if root node and 0 otherwise
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

typedef struct {
    char valid;
    string filenanme;
    unsigned int nodeid;
} location;

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
    return 1;
}

unsigned int getNodeIdFromName(int fileDesc,string name,unsigned int nodeid,int *found) {
    *found = 0;
    MDS data;
    // Seek to the current block metadata
    lseek(fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    // Get it's metadata
    read(fileDesc,&data,sizeof(MDS));
    // Determine data type
    printf("Searching for %s at node %u\n",name,nodeid);
    if (data.type == TYPE_DIRECTORY) {
        // Directory
        // Search all the entities until we find the id of the wanted one
        unsigned int i,curId;
        MDS tmpData;
        for (i = 0; i < data.size/sizeof(int); i++) {
            // Get id of the current entity
            curId = *(unsigned int*)(data.data.datablocks + i*sizeof(int));
            // Seek to the current entity metadata
            lseek(fileDesc,sizeof(superblock) + curId * sizeof(MDS),SEEK_SET);
            // Get it's metadata
            read(fileDesc,&tmpData,sizeof(MDS));
            // Check if the name matches
            printf("\t%s\n",tmpData.filename);
            if (!strcmp(name,tmpData.filename)) {
                // Found
                *found = 1;
                return curId;
            }
        }
    } else if (data.type == TYPE_SHORTCUT) {
        // Shortcut
        // Simply return the shortcut's node id destination
        *found = 1;
        return *(unsigned int*)(data.data.datablocks);
    } 
    // Not found
    return 0;
}

location getPathLocation(int fileDesc,string path,unsigned int nodeid,int ignoreLastEntity) {
    string entityName = strtok(path,"/");
    // Determine path type
    if (path[0] == '/') {
        // Absolute path so start searching from the root
        nodeid = 0;
    }
    int found;
    location ret;
    while (entityName != NULL){
        nodeid = getNodeIdFromName(fileDesc,entityName,nodeid,&found);
        ret.filenanme = entityName;
        // Not found
        if (!found) {
            // Check if we reached the last entity and we want to ignore it (for mkdir,touch,cp,cat,ln,mv commands)
            if (ignoreLastEntity && strtok(NULL,"/") == NULL) {
                ret.nodeid = nodeid;
                ret.valid = 1;
            } else {
                ret.valid = 0;
                ret.nodeid = 0;
            }
            break;
        } else {
            ret.nodeid = nodeid;
            ret.valid = 1;
            entityName = strtok(NULL,"/");
        }
    }
    return ret;
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
    data.root = 0;
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
    data.root = 0;
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

int CFS_CreateFile(int fileDesc,string name,unsigned int dirnodeid,string content) {
    MDS data;
    // Initialize metadata bytes to 0 to avoid valgrind errors
    memset(&data,0,sizeof(MDS));
    // Initialize shortcut metadata
    data.deleted = 0;
    data.root = 0;
    data.nodeid = CFS_GetNextAvailableNodeId(fileDesc);
    strcpy(data.filename,name);
    data.size = 0;
    data.type = TYPE_FILE;
    data.parent_nodeid = dirnodeid;
    time_t timer = time(NULL);
    data.creation_time = data.accessTime = data.modificationTime = timer;
    // Write content to datablocks
    memcpy(data.data.datablocks,content,strlen(content) + 1);
    // Write file data to cfs file
    write(fileDesc,&data,sizeof(MDS));
    // Write file descriptor to directory's node list
    lseek(fileDesc,sizeof(superblock) + dirnodeid * sizeof(MDS),SEEK_SET);
    MDS locationData;
    read(fileDesc,&locationData,sizeof(MDS));
    memcpy(locationData.data.datablocks + locationData.size,&data.nodeid,sizeof(int));
    locationData.size += sizeof(int);
    lseek(fileDesc,-sizeof(MDS),SEEK_CUR);
    write(fileDesc,&locationData,sizeof(MDS));
    return 1;
}

int CFS_ModifyFileTimestamps(int fileDesc,unsigned int nodeid,int access,int modification) {
    MDS data;
    // Seek to file's metadata location in cfs file
    lseek(fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    // Read file's metadata
    read(fileDesc,&data,sizeof(MDS));
    // Modify the timestamps
    time_t timestamp = time(NULL);
    if (access)
        data.accessTime = timestamp;
    if (modification)
        data.modificationTime = timestamp;
    // Seek again to file's metadata location in cfs file
    lseek(fileDesc,-sizeof(MDS),SEEK_CUR);
    // Write changes to cfs file
    write(fileDesc,&data,sizeof(MDS));
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
        data.root = 1;
        data.nodeid = 0;
        strcpy(data.filename,"/");
        data.size = 0;
        data.type = TYPE_DIRECTORY;
        data.parent_nodeid = 0;
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

void CFS_pwd(int fileDesc,unsigned int nodeid) {
    // Seek to current node's metadata in cfs file
    MDS data;
    lseek(fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    // Read the metadata from the cfs file
    read(fileDesc,&data,sizeof(MDS));
    if (!data.root) {
        CFS_pwd(fileDesc,data.parent_nodeid);
        printf("/%s",data.filename);
    } else {
        printf("%s",data.filename);
    }
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
                    location loc;
                    // Read paths for new directories
                    while (!lastword) {
                        dir = readNextWord(&lastword);
                        // Get location for the new directory
                        loc = getPathLocation(cfs->fileDesc,dir,cfs->currentDirectoryId,1);
                        // Check if path exists
                        if (loc.valid) {
                            // Path exists so create the new directory there
                            CFS_CreateDirectory(cfs->fileDesc,loc.filenanme,loc.nodeid);
                        } else {
                            // Path does not exist so throw an error
                            printf("No such file or directory.\n");
                        }
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
        // Create new file
        else if (!strcmp("cfs_touch",commandLabel)) {
            // Check if options or files were specified
            if (!lastword) {
                int ok = 1;
                // Read options
                int options[2] = {0,0};
                string option = readNextWord(&lastword);
                int optionscount = 0;
                while (!strcmp("-a",option) || !strcmp("-m",option)) {
                    if (lastword) {
                        ok = 0;
                        DestroyString(&option);
                        break;
                    }
                    // Modify access time only option
                    if (!strcmp("-a",option)) {
                        options[TOUCH_ACCESS] = 1;
                    }
                    else if (!strcmp("-m",option)) {
                        options[TOUCH_MODIFICATION] = 1;
                    }
                    DestroyString(&option);
                    option = readNextWord(&lastword);
                    optionscount++;
                }
                if (ok) {
                    // No options so activate both by default
                    if (!(options[TOUCH_ACCESS] || options[TOUCH_MODIFICATION])) {
                        options[TOUCH_ACCESS] = options[TOUCH_MODIFICATION] = 1;
                    }
                    // Read and create files
                    string file = option;
                    location loc;
                    while (1) {
                        // Check if file exists
                        loc = getPathLocation(cfs->fileDesc,file,cfs->currentDirectoryId,0);
                        if (loc.valid) {
                            // File exists so just modify it's timestamps
                            CFS_ModifyFileTimestamps(cfs->fileDesc,loc.nodeid,options[TOUCH_ACCESS],options[TOUCH_MODIFICATION]);
                        } else {
                            // File does not exist so create it
                            // Get location for the new file
                            loc = getPathLocation(cfs->fileDesc,file,cfs->currentDirectoryId,1);
                            // Check if path exists
                            if (loc.valid) {
                                // Path exists so create the new file there
                                CFS_CreateFile(cfs->fileDesc,loc.filenanme,loc.nodeid,"");
                            } else {
                                // Path does not exist so throw an error
                                printf("No such file or directory.\n");
                            }
                        }
                        DestroyString(&file);
                        if (lastword)
                            break;
                        file = readNextWord(&lastword);
                    }
                } else {
                    // No file(s) specified
                    printf("Usage:cfs_touch <OPTIONS> <FILES>\n");
                }
            } else {
                printf("Usage:cfs_touch <OPTIONS> <FILES>\n");
            }
        }
        // Print working directory (absolute path)
        else if (!strcmp("cfs_pwd",commandLabel)) {
            if (cfs->fileDesc != -1) {
                CFS_pwd(cfs->fileDesc,cfs->currentDirectoryId);
            } else {
                printf("Not currently working with a cfsd file.");
            }
            printf("\n");
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
            IgnoreRemainingInput();
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
        free(*cfs);
        *cfs = NULL;
        return 1;
    }
    // Not yet initialize
    return 0;
}
