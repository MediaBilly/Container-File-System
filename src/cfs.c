#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <libgen.h>
#include "../headers/cfs.h"
#include "../headers/string_functions.h"
#include "../headers/minheap.h"
#include "../headers/queue.h"

// Define file types
#define TYPE_FILE 0
#define TYPE_DIRECTORY 1
#define TYPE_SHORTCUT 2

// Define touch option flags
#define TOUCH_ACCESS 0
#define TOUCH_MODIFICATION 1

// Define ls option flags
#define LS_ALL_FILES 0
#define LS_RECURSIVE_PRINT 1
#define LS_ALL_ATTRIBUTES 2
#define LS_UNORDERED 3
#define LS_DIRECTORIES_ONLY 4
#define LS_LINKS_ONLY 5

// CFS structure definition
struct cfs {
    int fileDesc; // File descriptor of currently working cfs file
    char currentFile[MAX_FILENAME_SIZE]; // Name of currently working cfs file
    int currentDirectoryId; // Nodeid for current directory
    int BLOCK_SIZE;
    int FILENAME_SIZE;
    int MAX_FILE_SIZE;
    int MAX_DIRECTORY_FILE_NUMBER;
};

// Superblock definition
typedef struct {
    int BLOCK_SIZE;
    int FILENAME_SIZE;
    int MAX_FILE_SIZE;
    int MAX_DIRECTORY_FILE_NUMBER;
} superblock;

typedef struct {
    char valid;
    string filenanme;
    unsigned int nodeid;
    unsigned int type;
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
    setlocale(LC_TIME, "el_GR.utf8");
    return 1;
}

unsigned int getNodeIdFromName(int fileDesc,string name,unsigned int nodeid,int *found,unsigned int *type) {
    *found = 1;
    MDS data;
    // Seek to the current block metadata
    lseek(fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    // Get it's metadata
    read(fileDesc,&data,sizeof(MDS));
    // Determine data type
    if (data.type == TYPE_DIRECTORY) {
        // Directory
        // Search all the entities until we find the id of the wanted one
        unsigned int i,curId;
        MDS tmpData;
        *found = 0;
        for (i = 0; i < data.size/sizeof(int); i++) {
            // Get id of the current entity
            curId = *(unsigned int*)(data.data.datablocks + i*sizeof(int));
            // Seek to the current entity metadata
            lseek(fileDesc,sizeof(superblock) + curId * sizeof(MDS),SEEK_SET);
            // Get it's metadata
            read(fileDesc,&tmpData,sizeof(MDS));
            // Check if the name matches
            if (!strcmp(name,tmpData.filename)) {
                // Found
                *found = 1;
                // If wanted node is shortcut return the id and type of the pointed node
                if (tmpData.type == TYPE_SHORTCUT) {
                    // Get pointed node metadata
                    lseek(fileDesc,sizeof(superblock) + *(unsigned int*)(tmpData.data.datablocks)*sizeof(MDS),SEEK_SET);
                    read(fileDesc,&tmpData,sizeof(MDS));
                    curId = tmpData.nodeid;
                } 
                *type = tmpData.type;
                return curId;
            }
        }
    } 
    return data.nodeid;
}

// Checks if an entity with a specific name exists in a specific directory
int exists(int fileDesc,string name,unsigned int dirnodeid) {
    int found;
    unsigned int type;
    getNodeIdFromName(fileDesc,name,dirnodeid,&found,&type);
    return found;
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
    ret.nodeid = 0;
    ret.filenanme = "/";
    ret.type = TYPE_DIRECTORY;
    ret.valid = 1;
    while (entityName != NULL){
        nodeid = getNodeIdFromName(fileDesc,entityName,nodeid,&found,&ret.type);
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

unsigned int CFS_CreateShortcut(int fileDesc,string name,unsigned int nodeid,unsigned int destNodeId) {
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
    memcpy(locationData.data.datablocks + locationData.size,&data.nodeid,sizeof(unsigned int));
    locationData.size += sizeof(unsigned int);
    lseek(fileDesc,-sizeof(MDS),SEEK_CUR);
    write(fileDesc,&locationData,sizeof(MDS));
    return data.nodeid;
}

unsigned int CFS_CreateDirectory(int fileDesc,string name,unsigned int nodeid) {
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
    return data.nodeid;
}

unsigned int CFS_CreateFile(int fileDesc,string name,unsigned int dirnodeid,char content[DATABLOCK_NUM],int size) {
    MDS data;
    // Initialize metadata bytes to 0 to avoid valgrind errors
    memset(&data,0,sizeof(MDS));
    // Initialize shortcut metadata
    data.deleted = 0;
    data.root = 0;
    data.nodeid = CFS_GetNextAvailableNodeId(fileDesc);
    strcpy(data.filename,name);
    data.size = size;
    data.type = TYPE_FILE;
    data.parent_nodeid = dirnodeid;
    time_t timer = time(NULL);
    data.creation_time = data.accessTime = data.modificationTime = timer;
    // Write content to datablocks
    memcpy(data.data.datablocks,content,size);
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
    return data.nodeid;
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
    int fd = -1;
    // Check if sizes satisfy constraints
    if (MAX_FILE_SIZE <= DATABLOCK_NUM && FILENAME_SIZE <= MAX_FILENAME_SIZE && MAX_DIRECTORY_FILE_NUMBER <= MAX_FILE_SIZE/sizeof(unsigned int) && BLOCK_SIZE <= MAX_FILE_SIZE) {
        // Create the file
        fd = open(pathname,O_RDWR|O_CREAT|O_TRUNC,FILE_PERMISSIONS);
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
    } else {
        printf("Constraints are not satisfied.\n");
    }
    return fd;
}

void CFS_pwd(int fileDesc,unsigned int nodeid,int last) {
    // Seek to current node's metadata in cfs file
    MDS data;
    lseek(fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    // Read the metadata from the cfs file
    read(fileDesc,&data,sizeof(MDS));
    if (!data.root) {
        CFS_pwd(fileDesc,data.parent_nodeid,0);
        printf("%s",data.filename);
        if (!last)
            printf("/");
        else
            printf("\n");
    } else {
        printf("%s",data.filename);
        if (last)
            printf("\n");
    }
}

void CFS_PrintFileInfo(MDS data,int options[6]) {
    // Ignore hidden files if -a option was not specified
    if (!options[LS_ALL_FILES] && data.filename[0] == '.')
        return;
    // If -d option (only directories) was specified ignore other types
    if (options[LS_DIRECTORIES_ONLY] && data.type != TYPE_DIRECTORY)
        return;
    // If -h option (only links) was specified ignore other types
    if (options[LS_LINKS_ONLY] && data.type != TYPE_SHORTCUT)
        return;
    if (options[LS_ALL_ATTRIBUTES]) {
        switch (data.type) {
            case TYPE_DIRECTORY:
                printf("dir ");
                break;
            case TYPE_SHORTCUT:
                printf("link");
                break;
            case TYPE_FILE:
                printf("file");
                break;
            default:
                printf("    ");
                break;
        }
        char creationTime[70],accessTime[70],modificationTime[70];
        strftime(creationTime,sizeof(creationTime),"%c",localtime(&data.creation_time));
        strftime(accessTime,sizeof(accessTime),"%c",localtime(&data.accessTime));
        strftime(modificationTime,sizeof(modificationTime),"%c",localtime(&data.modificationTime));
        printf(" %s %s %s %d %s\n",creationTime,accessTime,modificationTime,data.size,data.filename);
    } else {
        printf("%s ",data.filename);
    }
}

void CFS_ls(int fileDesc,unsigned int nodeid,int options[6],string path) {
    MDS data;
    // Seek to the wanted block metadata
    lseek(fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    // Get it's metadata
    read(fileDesc,&data,sizeof(MDS));
    // Determine data type
    if (data.type == TYPE_DIRECTORY) {
        // Directory so show all the contents of the directory
        // Show info for all the directory's entities
        unsigned int i,curId;
        MDS tmpData;
        MinHeap fileHeap;
        // If we do not have the unorderedoption create a minheap to sort the contents
        if (!options[LS_UNORDERED])
            fileHeap = MinHeap_Create(data.size/sizeof(int));
        // In recursive directory option print the current path
        if (options[LS_RECURSIVE_PRINT])
            printf("%s:\n",path);
        for (i = 0; i < data.size/sizeof(int); i++) {
            // Get id of the current entity
            curId = *(unsigned int*)(data.data.datablocks + i*sizeof(int));
            // Seek to the current entity metadata
            lseek(fileDesc,sizeof(superblock) + curId * sizeof(MDS),SEEK_SET);
            // Get it's metadata
            read(fileDesc,&tmpData,sizeof(MDS));
            // If we want ordered print store all the contents in a minheap and we will print them later
            if (!options[LS_UNORDERED]) {
                MinHeap_Insert(fileHeap,tmpData);
            } else {
                // Otherwise just print entity info
                CFS_PrintFileInfo(tmpData,options);
            }
        }
        // Print all the contents ordered if -u is not enabled
        if (!options[LS_UNORDERED]) {
            MDS tmp;
            int empty = 0;
            while (!empty){
                tmp = MinHeap_ExtractMin(fileHeap,&empty);
                if (!empty)
                    CFS_PrintFileInfo(tmp,options);
            }
            MinHeap_Destroy(&fileHeap);
        }
        // In recursive print option recursively print all subfolder's contents
        if (options[LS_RECURSIVE_PRINT]) {
            for (i = 0; i < data.size/sizeof(int); i++) {
                // Get id of the current entity
                curId = *(unsigned int*)(data.data.datablocks + i*sizeof(int));
                // Seek to the current entity metadata
                lseek(fileDesc,sizeof(superblock) + curId * sizeof(MDS),SEEK_SET);
                // Get it's metadata
                read(fileDesc,&tmpData,sizeof(MDS));
                // Recursively ls only on directories
                if (tmpData.type == TYPE_DIRECTORY) {
                    string newPath = copyString(path);
                    stringAppend(&newPath,"/");
                    stringAppend(&newPath,tmpData.filename);
                    CFS_ls(fileDesc,tmpData.nodeid,options,newPath);
                    DestroyString(&newPath);
                }
            }
        }
    } else if (data.type == TYPE_SHORTCUT) {
        // Get pointed node metadata
        MDS destData;
        lseek(fileDesc,sizeof(superblock) + *(unsigned int*)(data.data.datablocks)*sizeof(MDS),SEEK_SET);
        read(fileDesc,&destData,sizeof(MDS));
        // Print destination contents
        CFS_ls(fileDesc,destData.nodeid,options,destData.filename);
    } else {
        CFS_PrintFileInfo(data,options);
    }
    if (!options[LS_ALL_ATTRIBUTES])
        printf("\n");
}

string getEntityNameFromPath(string path) {
    string token = strtok(path,"/");
    string name;
    while (token != NULL) {
        name = token;
        token = strtok(NULL,"/");
    }
    return name;
}

int CFS_ImportFile(CFS cfs,string source,unsigned int nodeid) {
    // Check if file exists in cfs
    if (!exists(cfs->fileDesc,getEntityNameFromPath(source),nodeid)) {
        // Open linux file
        int fd = open(source,O_RDONLY);
        // Get linux file size in bytes
        unsigned int size = lseek(fd,0L,SEEK_END);
        lseek(fd,0L,SEEK_SET);
        // Check if linux file fits in cfs
        string filename = getEntityNameFromPath(source);
        if (size <= cfs->MAX_FILE_SIZE) {
            // Linux file fits in cfs
            // Read it's content
            char bytes[DATABLOCK_NUM];
            read(fd,bytes,size);
            //printf("%s\n",bytes);
            // Create the corresponding file in cfs
            CFS_CreateFile(cfs->fileDesc,filename,nodeid,bytes,size);
        } else {
            // Linux file does not fit in cfs
            printf("File %s does not fit in cfs.\n",filename);
            return 0;
        }
        // Close linux file
        close(fd);
    } else {
        printf("File %s already exists\n",getEntityNameFromPath(source));
        return 0;
    }
    return 1;
}

int CFS_ImportDirectory(CFS cfs,string source,unsigned int nodeid) {
    struct stat entryinfo;
    DIR *dirp;
    struct dirent *dirContent;
    // Open linux directory
    dirp = opendir(source);
    // Read and import all entries
    while ((dirContent = readdir(dirp)) != NULL) {
        // Ignore . , .. directories and deleted entities
        if (!strcmp(".",dirContent->d_name) || !strcmp("..",dirContent->d_name) || dirContent->d_ino == 0)
            continue;
        // Recursively import all elements in linux directory 
        string dirContentPath = copyString(source);
        stringAppend(&dirContentPath,"/");
        stringAppend(&dirContentPath,dirContent->d_name);
        // Get entry type
        if (stat(dirContentPath,&entryinfo) != -1) {
            if (S_ISDIR(entryinfo.st_mode)) {
                // Directory
                // Check if corresponding directory exists
                if (!exists(cfs->fileDesc,dirContent->d_name,nodeid)) {
                    // Create corresponding directory in cfs
                    unsigned int dirNodeId = CFS_CreateDirectory(cfs->fileDesc,dirContent->d_name,nodeid);
                    // Recursively import linux directory's content to cfs directory
                    CFS_ImportDirectory(cfs,dirContentPath,dirNodeId);
                } else {
                    printf("File %s already exists\n",dirContent->d_name);
                }
            } else if (S_ISREG(entryinfo.st_mode)) {
                // Regular file
                CFS_ImportFile(cfs,dirContentPath,nodeid);
            } else {
                printf("Unknown file type of %s\n",source);
            }
        } else {
            perror("Failed  to get  file  status");
        }
        DestroyString(&dirContentPath);
    }
    // Close linux directory
    closedir(dirp);
    return 1;
}

int CFS_ImportSource(CFS cfs,string source,unsigned int nodeid) {
    struct stat sourceinfo;
    // Get source type
    if (stat(source,&sourceinfo) != -1) {
        if (S_ISDIR(sourceinfo.st_mode)) {
            // Directory
            CFS_ImportDirectory(cfs,source,nodeid);
        } else if (S_ISREG(sourceinfo.st_mode)) {
            // Regular file
            CFS_ImportFile(cfs,source,nodeid);
        } else {
            printf("Unknown file type of %s\n",source);
            return 0;
        }
    } else {
        perror("Failed  to get  file  status");
        return 0;
    }
    return 1;
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
                    // Read file's parameters from superblock
                    lseek(cfs->fileDesc,0L,SEEK_SET);
                    superblock sb;
                    read(cfs->fileDesc,&sb,sizeof(superblock));
                    cfs->BLOCK_SIZE = sb.BLOCK_SIZE;
                    cfs->FILENAME_SIZE = sb.FILENAME_SIZE;
                    cfs->MAX_DIRECTORY_FILE_NUMBER = sb.MAX_DIRECTORY_FILE_NUMBER;
                    cfs->MAX_FILE_SIZE = sb.MAX_FILE_SIZE;
                }
                DestroyString(&file);
            } else {
                // File not specified
                printf("Usage:cfs_workwith <FILE>\n");
            }
        }
        // Create directory (or directories)
        else if (!strcmp("cfs_mkdir",commandLabel)) {
            // Check if we have an open file to work on
            if (cfs->fileDesc != -1) {
                // Check if directories were specified
                if (!lastword) {
                    string dir;
                    location loc;
                    // Read paths for new directories
                    while (!lastword) {
                        dir = readNextWord(&lastword);
                        // Check if directory exists
                        string dirCopy = copyString(dir);
                        loc = getPathLocation(cfs->fileDesc,dirCopy,cfs->currentDirectoryId,0);
                        if (!loc.valid) {
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
                        } else {
                            printf("File %s already exists\n",dir);
                        }
                        DestroyString(&dirCopy);
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
            // Check if we have an open file to work on
            if (cfs->fileDesc != -1) {
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
                        string file = option,filecopy;
                        location loc;
                        while (1) {
                            filecopy = copyString(file);
                            // Check if file exists
                            loc = getPathLocation(cfs->fileDesc,file,cfs->currentDirectoryId,0);
                            if (loc.valid) {
                                // File exists so just modify it's timestamps
                                CFS_ModifyFileTimestamps(cfs->fileDesc,loc.nodeid,options[TOUCH_ACCESS],options[TOUCH_MODIFICATION]);
                            } else {
                                // File does not exist so create it
                                // Get location for the new file
                                loc = getPathLocation(cfs->fileDesc,filecopy,cfs->currentDirectoryId,1);
                                // Check if path exists
                                if (loc.valid) {
                                    // Path exists so create the new file there
                                    CFS_CreateFile(cfs->fileDesc,loc.filenanme,loc.nodeid,"",0);
                                } else {
                                    // Path does not exist so throw an error
                                    printf("No such file or directory.\n");
                                }
                            }
                            DestroyString(&filecopy);
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
            } else {
                printf("Not currently working with a cfs file.\n");
                if (!lastword)
                    IgnoreRemainingInput();
            }
        }
        // Print working directory (absolute path)
        else if (!strcmp("cfs_pwd",commandLabel)) {
            // Check if we have an open file to work on
            if (lastword) {
                if (cfs->fileDesc != -1) {
                    CFS_pwd(cfs->fileDesc,cfs->currentDirectoryId,1);
                } else {
                    printf("Not currently working with a cfs file.\n");
                }
            } else {
                printf("Usage:cfs_pwd\n");
                IgnoreRemainingInput();
            }
        }
        // Change directory
        else if (!strcmp("cfs_cd",commandLabel)) {
            // Check if we have an open file to work on
            if (cfs->fileDesc != -1) {
                // Check if path was specified
                if (!lastword) {
                    string path = readNextWord(&lastword);
                    // Check for correct usage (no other parameters)
                    if (lastword) {
                        // Correect usage so change working directory
                        location newdir = getPathLocation(cfs->fileDesc,path,cfs->currentDirectoryId,0);
                        if (newdir.valid) {
                            if (newdir.type == TYPE_DIRECTORY) {
                                cfs->currentDirectoryId = newdir.nodeid;
                            } else {
                                printf("Not a directory.\n");
                            }
                        } else {
                            printf("No such file or directory.\n");
                        }
                    } else {
                        // Incorrect usage
                        printf("Usage:cfs_cd <PATH>\n");
                    }
                    DestroyString(&path);
                } else {
                    // Path not specified
                    printf("Usage:cfs_cd <PATH>\n");
                }
            } else {
                printf("Not currently working with a cfs file.\n");
                IgnoreRemainingInput();
            }
        }
        // Print files or folder contents(ls)
        else if (!strcmp("cfs_ls",commandLabel)) {
            // Check if we have an open file to work on
            if (cfs->fileDesc != -1) {
                // Check if parameters were specified
                if (!lastword) {
                    // At least 1 parameter was specified
                    // Read options
                    int options[6] = {0,0,0,0,0,0};
                    // Read first option or file
                    string option = readNextWord(&lastword);
                    int ok = 1,lastwasoption = 0;
                    unsigned int optionsCount = 0;
                    while (option[0] == '-') {
                        if (!strcmp("-a",option)) {
                            options[LS_ALL_FILES] = 1;
                        } else if (!strcmp("-r",option)) {
                            options[LS_RECURSIVE_PRINT] = 1;
                        } else if (!strcmp("-l",option)) {
                            options[LS_ALL_ATTRIBUTES] = 1;
                        } else if (!strcmp("-u",option)) {
                            options[LS_UNORDERED] = 1;
                        } else if (!strcmp("-d",option)) {
                            if (options[LS_LINKS_ONLY]) {
                                printf("Links-only option was previously specified and directories-only option cannot be specified.\n");
                                ok = 0;
                            } else {
                                options[LS_DIRECTORIES_ONLY] = 1;
                            }
                        } else if (!strcmp("-h",option)) {
                            if (options[LS_DIRECTORIES_ONLY]) {
                                printf("Directories-only option was previously specified and links-only option cannot be specified.\n");
                                ok = 0;
                            } else {
                                options[LS_LINKS_ONLY] = 1;
                            }
                        } else {
                            printf("Wrong option\n");
                            IgnoreRemainingInput();
                            ok = 0;
                        }
                        if (ok)
                            optionsCount++;
                        if (!ok || lastword) {
                            lastwasoption = 1;
                            break;
                        } else {
                            DestroyString(&option);
                            option = readNextWord(&lastword);
                        }
                    }
                    if (ok) {
                        if (optionsCount) {
                            if (!lastwasoption) {
                                string path = option,pathCopy;
                                // Read files (or directories)
                                location loc;
                                while (1) {
                                    pathCopy = copyString(path);
                                    loc = getPathLocation(cfs->fileDesc,path,cfs->currentDirectoryId,0);
                                    if (loc.valid) {
                                        CFS_ls(cfs->fileDesc,loc.nodeid,options,pathCopy);
                                    } else {
                                        printf("No such file or directory.\n");
                                    }
                                    DestroyString(&pathCopy);
                                    DestroyString(&path);
                                    if (!lastword) {
                                        path = readNextWord(&lastword);
                                    } else {
                                        break;
                                    }
                                }
                            } else {
                                // Only options were specified so list the current directory
                                CFS_ls(cfs->fileDesc,cfs->currentDirectoryId,options,".");
                                DestroyString(&option);
                            }
                        } else {
                            // Only directories specified
                            string path = option,pathCopy;
                            location loc;
                            while (1){
                                pathCopy = copyString(path);
                                loc = getPathLocation(cfs->fileDesc,option,cfs->currentDirectoryId,0);
                                if (loc.valid) {
                                    CFS_ls(cfs->fileDesc,loc.nodeid,options,pathCopy);
                                } else {
                                    printf("No such file or directory.\n");
                                }
                                DestroyString(&pathCopy);
                                DestroyString(&path);
                                if (!lastword) {
                                    path = readNextWord(&lastword);
                                } else {
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    // No parameters specified so list the current directory
                    int options[6] = {0,0,0,0,0,0}; // Default options
                    CFS_ls(cfs->fileDesc,cfs->currentDirectoryId,options,".");
                }
            } else {
                printf("Not currently working with a cfs file.\n");
                IgnoreRemainingInput();
            }
        }
        // Import linux files/directories to cfs
        else if (!strcmp("cfs_import",commandLabel)) {
            // Check if we have an open file to work on
            if (cfs->fileDesc != -1) {
                // Check if sources and destination directory were specified
                if (!lastword) {
                    // Read sources and add them to the queue
                    string argument = NULL;
                    Queue sourcesQueue;
                    Queue_Create(&sourcesQueue);
                    do {
                        DestroyString(&argument);
                        argument = readNextWord(&lastword);
                        if (!lastword)
                            Queue_Push(sourcesQueue,argument);
                    } while (!lastword);
                    // Read directory
                    string directory = argument;
                    // Get directory location in cfs
                    location loc = getPathLocation(cfs->fileDesc,directory,cfs->currentDirectoryId,0);
                    // Check if directory exists
                    if (loc.valid && loc.type == TYPE_DIRECTORY) {
                        // Read all sources from linux and import their contents in cfs
                        string source;
                        while (!Queue_Empty(sourcesQueue)) {
                            source = Queue_Pop(sourcesQueue);
                            CFS_ImportSource(cfs,source,loc.nodeid);
                            DestroyString(&source);
                        }
                        DestroyString(&directory);
                        Queue_Destroy(&sourcesQueue);
                    } else {
                        // Not a directory
                        printf("No such directory %s.\n",directory);
                    }
                } else {
                    // Nothing was specified
                    printf("Usage:cfs_import <SOURCES> ... <DIRECTORY>\n");
                }
            } else {
                printf("Not currently working with a cfs file.\n");
                IgnoreRemainingInput();
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
                    IgnoreRemainingInput();
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
            if (!lastword)
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
