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

// Define rm option flags
#define RM_PROMPT 0
#define RM_RECURSIVE 1

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
        for (i = 0; i < data.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)); i++) {
            // Get id of the current entity
            curId = *(unsigned int*)(data.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)));
            // Get name of the current entity
            string filename = data.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) + sizeof(unsigned int);
            // Seek to the current entity metadata
            lseek(fileDesc,sizeof(superblock) + curId * sizeof(MDS),SEEK_SET);
            // Get it's metadata
            read(fileDesc,&tmpData,sizeof(MDS));
            // Check if the name matches
            if (!strcmp(name,filename)) {
                // Found
                *found = 1; 
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
        if (data.deleted) {
            lseek(fileDesc,-sizeof(MDS),SEEK_CUR);
            return data.nodeid;
        }
    }
    // End of file reached so new node will be placed there
    return data.nodeid + 1;
}

unsigned int CFS_CreateDirectory(CFS cfs,string name,unsigned int nodeid) {
    // Get location directory data
    MDS locationData;
    lseek(cfs->fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    read(cfs->fileDesc,&locationData,sizeof(MDS));
    // Check if new directory fits in directory
    if (locationData.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) > cfs->MAX_DIRECTORY_FILE_NUMBER)
        return 0;
    MDS data;
    // Initialize metadata bytes to 0 to avoid valgrind errors
    memset(&data,0,sizeof(MDS));
    // Initialize directory metadata
    data.deleted = 0;
    data.root = 0;
    data.links = 0;
    data.nodeid = CFS_GetNextAvailableNodeId(cfs->fileDesc);
    strcpy(data.filename,name);
    data.size = 0;
    data.type = TYPE_DIRECTORY;
    data.parent_nodeid = nodeid;
    time_t timer = time(NULL);
    data.creation_time = data.accessTime = data.modificationTime = timer;
    // Create . shortcut (hardlink)
    memcpy(data.data.datablocks,&data.nodeid,sizeof(unsigned int));
    strcpy(data.data.datablocks + sizeof(unsigned int),".");
    // Create .. shortcut (hardlink)
    memcpy(data.data.datablocks + sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char),&data.parent_nodeid,sizeof(unsigned int));
    strcpy(data.data.datablocks + 2*sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char),"..");
    data.size += 2*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char));
    // Write directory data to cfs file
    write(cfs->fileDesc,&data,sizeof(MDS));
    // Write directory descriptor and name to node's list
    memcpy(locationData.data.datablocks + locationData.size,&data.nodeid,sizeof(unsigned int));
    strcpy(locationData.data.datablocks + locationData.size + sizeof(unsigned int),name);
    locationData.size += (sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char));
    lseek(cfs->fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    write(cfs->fileDesc,&locationData,sizeof(MDS));
    return data.nodeid;
}

unsigned int CFS_CreateFile(CFS cfs,string name,unsigned int dirnodeid,char content[DATABLOCK_NUM],int size) {
    // Get location directory data
    MDS locationData;
    lseek(cfs->fileDesc,sizeof(superblock) + dirnodeid * sizeof(MDS),SEEK_SET);
    read(cfs->fileDesc,&locationData,sizeof(MDS));
    // Check if new file fits in directory
    if (locationData.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) > cfs->MAX_DIRECTORY_FILE_NUMBER)
        return 0;
    MDS data;
    // Initialize metadata bytes to 0 to avoid valgrind errors
    memset(&data,0,sizeof(MDS));
    // Initialize file metadata
    data.deleted = 0;
    data.root = 0;
    data.links = 0;
    data.nodeid = CFS_GetNextAvailableNodeId(cfs->fileDesc);
    strcpy(data.filename,name);
    data.size = size;
    data.type = TYPE_FILE;
    data.parent_nodeid = dirnodeid;
    time_t timer = time(NULL);
    data.creation_time = data.accessTime = data.modificationTime = timer;
    // Write content to datablocks
    memcpy(data.data.datablocks,content,size);
    // Write file data to cfs file
    write(cfs->fileDesc,&data,sizeof(MDS));
    // Write file descriptor and name to directory's node list
    memcpy(locationData.data.datablocks + locationData.size,&data.nodeid,sizeof(unsigned int));
    strcpy(locationData.data.datablocks + locationData.size + sizeof(unsigned int),name);
    locationData.size += (sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char));
    lseek(cfs->fileDesc,sizeof(superblock) + dirnodeid * sizeof(MDS),SEEK_SET);
    write(cfs->fileDesc,&locationData,sizeof(MDS));
    return data.nodeid;
}

unsigned int CFS_CreateHardLink(CFS cfs,string outputfilename,unsigned int sourcenodeid,unsigned int dirnodeid) {
    // Get parent directory data
    MDS parentData;
    lseek(cfs->fileDesc,sizeof(superblock) + dirnodeid * sizeof(MDS),SEEK_SET);
    read(cfs->fileDesc,&parentData,sizeof(MDS));
    // Check if new link fits in directory
    if (parentData.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) > cfs->MAX_DIRECTORY_FILE_NUMBER)
        return 0;
    // Write shortcut descriptor to parent directory's node list
    memcpy(parentData.data.datablocks + parentData.size,&sourcenodeid,sizeof(unsigned int));
    strcpy(parentData.data.datablocks + parentData.size + sizeof(unsigned int),outputfilename);
    parentData.size += (sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char));
    lseek(cfs->fileDesc,-sizeof(MDS),SEEK_CUR);
    write(cfs->fileDesc,&parentData,sizeof(MDS));
    // Get source node id data
    MDS sourceData;
    lseek(cfs->fileDesc,sizeof(superblock) + sourcenodeid * sizeof(MDS),SEEK_SET);
    read(cfs->fileDesc,&sourceData,sizeof(MDS));
    // Increase source # of links
    sourceData.links++;
    // Write updated source data back to cfs file
    lseek(cfs->fileDesc,-sizeof(MDS),SEEK_CUR);
    write(cfs->fileDesc,&sourceData,sizeof(MDS));
    return 1;
}

// Determines whether a directory is empty or not
int CFS_DirectoryIsEmpty(int fileDesc,unsigned int nodeId) {
    //Get directory data
    MDS data;
    lseek(fileDesc,sizeof(superblock) + nodeId * sizeof(MDS),SEEK_SET);
    read(fileDesc,&data,sizeof(MDS));
    // A cfs directory is empty only when it's only contents are . and .. shortcuts
    return data.type == TYPE_DIRECTORY && data.size == 2*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char));
}

// Decreases link count or marks node as deleted
int CFS_RemoveEntity(int fileDesc,unsigned int nodeId) {
    // Cannot remove root directory
    if (nodeId == 0) {
        return 0;
    }
    // Seek to the directory where the file is located
    lseek(fileDesc,sizeof(superblock) + nodeId * sizeof(MDS),SEEK_SET);
    MDS data;
    read(fileDesc,&data,sizeof(MDS));
    // If node is linked into 1 file mark it as deleted
    if (data.links == 0)
        data.deleted = 1;
    // If the node is hard-linked into more than 1 names decrease the links number
    else
        data.links--;
    // Write updated data to cfs
    lseek(fileDesc,-sizeof(MDS),SEEK_CUR);
    write(fileDesc,&data,sizeof(MDS));
    return 1;
}

int CFS_RemoveDirectoryContent(int fileDesc,unsigned int dirnodeid,int options[2]) {
    // Seek to the directory location
    lseek(fileDesc,sizeof(superblock) + dirnodeid * sizeof(MDS),SEEK_SET);
    MDS dirData;
    read(fileDesc,&dirData,sizeof(MDS));
    // Loop through all the files and directories ignoring . and .. locations
    unsigned int i,curId,delete,deletions = 0;
    MDS tmpData;
    for (i = 0; i < dirData.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char));) {
        delete = 0;
        // Get id of the current entity
        curId = *(unsigned int*)(dirData.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)));
        // Get name of the current entity
        string filename = dirData.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) + sizeof(unsigned int);
        // Ignore . and .. directories to avoid glitches and possible infinite loop
        if (!strcmp(".",filename) || !strcmp("..",filename)) {
            i++;
            continue;
        }
        // Seek to the current entity metadata
        lseek(fileDesc,sizeof(superblock) + curId * sizeof(MDS),SEEK_SET);
        // Get it's metadata
        read(fileDesc,&tmpData,sizeof(MDS));
        // Determine type
        if (tmpData.type == TYPE_DIRECTORY) {
            // Directory so remove empty sub-directories and if -r option is enabled remove content from non empty sub-directories
            if (CFS_DirectoryIsEmpty(fileDesc,curId)) {
                CFS_RemoveEntity(fileDesc,curId);
                delete = 1;
            } else {
                if (options[RM_RECURSIVE]) {
                    CFS_RemoveDirectoryContent(fileDesc,curId,options);
                }
            }
        } else if (tmpData.type == TYPE_FILE) {
            CFS_RemoveEntity(fileDesc,curId);
            delete = 1;
        }
        // If entity was deleted move the next (id,name) tuples 1 place left
        if (delete) {
            // If prompt(-i option) is enabled ask the user before deleting
            char answer;
            if (options[RM_PROMPT]) {
                do {
                    printf("Remove '%s'?(y/n)",filename);
                    answer = getPromptAnswer();
                } while (answer != 'n' && answer != 'y');
            } else {
                answer = 'y';
            }
            if (answer == 'y') {
                if (i < dirData.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) - 1)
                    memcpy(dirData.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)),dirData.data.datablocks + (i+1)*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)),(dirData.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) - i - 1)*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)));
                dirData.size -= (sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char));
                deletions++;
            } else {
                i++;
            }
        } else {
            i++;
        }
    }
    // Write changes (if any occured) to cfs file
    if (deletions) {
        lseek(fileDesc,sizeof(superblock) + dirnodeid * sizeof(MDS),SEEK_SET);
        write(fileDesc,&dirData,sizeof(MDS));
    }
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
    int fd = -1;
    // Check if sizes satisfy constraints
    if (MAX_FILE_SIZE <= DATABLOCK_NUM && FILENAME_SIZE <= MAX_FILENAME_SIZE && MAX_DIRECTORY_FILE_NUMBER <= MAX_FILE_SIZE/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) && BLOCK_SIZE <= MAX_FILE_SIZE) {
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
            // Create . shortcut (hardlink)
            memcpy(data.data.datablocks,&data.nodeid,sizeof(unsigned int));
            strcpy(data.data.datablocks + sizeof(unsigned int),".");
            // Create .. shortcut (hardlink)
            memcpy(data.data.datablocks + sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char),&data.parent_nodeid,sizeof(unsigned int));
            strcpy(data.data.datablocks + 2*sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char),"..");
            data.size += 2*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char));
            lseek(fd,0,SEEK_END);
            write(fd,&data,sizeof(MDS));
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

void CFS_PrintFileInfo(int fileDesc,MDS data,string filename,int options[6]) {
    // Ignore hidden files if -a option was not specified
    if (!options[LS_ALL_FILES] && data.filename[0] == '.')
        return;
    // If -d option (only directories) was specified ignore other types
    if (options[LS_DIRECTORIES_ONLY] && data.type != TYPE_DIRECTORY)
        return;
    // If -h option (only links) was specified ignore other types
    if (options[LS_LINKS_ONLY] && data.links == 0)
        return;
    if (options[LS_ALL_ATTRIBUTES]) {
        switch (data.type) {
            case TYPE_DIRECTORY:
                printf("dir ");
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
        printf(" %s %s %s %d %s\n",creationTime,accessTime,modificationTime,data.size,filename);
    } else {
        printf("%s ",data.filename);
    }
}

MDS getMetadataFromNodeId(int fileDesc,unsigned int nodeid) {
    MDS data;
    // Seek to the wanted block metadata
    lseek(fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    // Get it's metadata
    read(fileDesc,&data,sizeof(MDS));
    // Return the metadata
    return data;
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
        for (i = 0; i < data.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)); i++) {
            // Get id of the current entity
            curId = *(unsigned int*)(data.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)));
            // Get name of the current entity
            string filename = data.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) + sizeof(unsigned int);
            // Seek to the current entity metadata
            lseek(fileDesc,sizeof(superblock) + curId * sizeof(MDS),SEEK_SET);
            // Get it's metadata
            read(fileDesc,&tmpData,sizeof(MDS));
            // If we want ordered print store all the contents in a minheap and we will print them later
            if (!options[LS_UNORDERED]) {
                MinHeap_Insert(fileHeap,tmpData,filename);
            } else {
                // Otherwise just print entity info
                CFS_PrintFileInfo(fileDesc,tmpData,filename,options);
            }
        }
        // Print all the contents ordered if -u is not enabled
        if (!options[LS_UNORDERED]) {
            MDS tmp;
            int empty = 0;
            while (!empty){
                tmp = MinHeap_ExtractMin(fileHeap,&empty);
                if (!empty)
                    CFS_PrintFileInfo(fileDesc,tmp,tmp.filename,options);
            }
            MinHeap_Destroy(&fileHeap);
        }
        // In recursive print option recursively print all subfolder's contents
        if (options[LS_RECURSIVE_PRINT]) {
            for (i = 0; i < data.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)); i++) {
                // Get id of the current entity
                curId = *(unsigned int*)(data.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)));
                // Get name of the current entity
                string filename = data.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) + sizeof(unsigned int);
                // Seek to the current entity metadata
                lseek(fileDesc,sizeof(superblock) + curId * sizeof(MDS),SEEK_SET);
                // Get it's metadata
                read(fileDesc,&tmpData,sizeof(MDS));
                // Recursively ls only on directories (except . and .. shortcuts to avoid infinite loop)
                if (tmpData.type == TYPE_DIRECTORY && strcmp(".",filename) && strcmp("..",filename)) {
                    string newPath = copyString(path);
                    stringAppend(&newPath,"/");
                    stringAppend(&newPath,tmpData.filename);
                    CFS_ls(fileDesc,tmpData.nodeid,options,newPath);
                    DestroyString(&newPath);
                }
            }
        }
    }
    if (!options[LS_ALL_ATTRIBUTES])
        printf("\n");
}

string getEntityNameFromPath(string path) {
    string pathCopy = copyString(path);
    string token = strtok(pathCopy,"/");
    string name;
    while (token != NULL) {
        name = token;
        token = strtok(NULL,"/");
    }
    name = copyString(name);
    DestroyString(&pathCopy);
    return name;
}

int CFS_ImportFile(CFS cfs,string source,unsigned int nodeid) {
    // Check if file exists in cfs
    string filename = getEntityNameFromPath(source);
    int ret = 1;
    if (!exists(cfs->fileDesc,filename,nodeid)) {
        // Open linux file
        int fd = open(source,O_RDONLY);
        // Get linux file size in bytes
        unsigned int size = lseek(fd,0L,SEEK_END);
        lseek(fd,0L,SEEK_SET);
        // Check if linux file fits in cfs
        if (size <= cfs->MAX_FILE_SIZE) {
            // Linux file fits in cfs
            // Read it's content
            char bytes[DATABLOCK_NUM];
            read(fd,bytes,size);
            //printf("%s\n",bytes);
            // Create the corresponding file in cfs
            if (!CFS_CreateFile(cfs,filename,nodeid,bytes,size)) {
                printf("Not enough space in cfs to import file %s\n",filename);
                ret = 0;
            }
        } else {
            // Linux file does not fit in cfs
            printf("File %s does not fit in cfs.\n",filename);
            ret = 0;
        }
        // Close linux file
        close(fd);
    } else {
        printf("File %s already exists\n",filename);
        ret = 0;
    }
    DestroyString(&filename);
    return ret;
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
                    unsigned int dirNodeId;
                    // Check if there is enough space for the new directory
                    if ((dirNodeId = CFS_CreateDirectory(cfs,dirContent->d_name,nodeid)) == 0) {
                        printf("Not enough space to create directory %s\n",dirContent->d_name);
                    } else {
                        // Recursively import linux directory's content to cfs directory
                        CFS_ImportDirectory(cfs,dirContentPath,dirNodeId);
                    }
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

int CFS_ExportFile(CFS cfs,unsigned int nodeid,string directory,string filename) {
    // Get file data
    MDS data;
    lseek(cfs->fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    read(cfs->fileDesc,&data,sizeof(MDS));
    // Determine export path
    string path = copyString(directory);
    stringAppend(&path,"/");
    stringAppend(&path,filename);
    // Create file in linux and check if creation was ok
    int fd;
    if ((fd = open(path,O_CREAT|O_WRONLY|O_TRUNC,FILE_PERMISSIONS)) != -1) {
        // Successful creation so write data
        write(fd,data.data.datablocks,data.size);
        // Close the file
        close(fd);
        DestroyString(&path);
        return 1;
    } else {
        DestroyString(&path);
        perror("File creation error");
        return 0;
    }
}

int CFS_ExportDirectory(CFS cfs,unsigned int nodeid,string directory) {
    // Get directory data
    MDS data;
    lseek(cfs->fileDesc,sizeof(superblock) + nodeid * sizeof(MDS),SEEK_SET);
    read(cfs->fileDesc,&data,sizeof(MDS));
    // Loop through all the entities
    unsigned int i,curId;
    MDS tmpData;
    string path;
    for (i = 0; i < data.size/(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)); i++) {
        // Get id of the current entity
        curId = *(unsigned int*)(data.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)));
        // Get name of the current entity
        string filename = data.data.datablocks + i*(sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char)) + sizeof(unsigned int);
        // Ignore . and .. shortcuts to avoid infinite loop
        if (!strcmp(".",filename) || !strcmp("..",filename))
            continue;
        // Seek to the current entity metadata
        lseek(cfs->fileDesc,sizeof(superblock) + curId * sizeof(MDS),SEEK_SET);
        // Get it's metadata
        read(cfs->fileDesc,&tmpData,sizeof(MDS));
        // Check it's type
        if (tmpData.type == TYPE_DIRECTORY) {
            // Directory
            // Create the corresponding directory in linux
            path = copyString(directory);
            stringAppend(&path,"/");
            stringAppend(&path,tmpData.filename);
            mkdir(path,FILE_PERMISSIONS);
            // Recursively export content of the current directory
            CFS_ExportDirectory(cfs,tmpData.nodeid,path);
            DestroyString(&path);
        } else if (tmpData.type == TYPE_FILE) {
            // Regular file
            CFS_ExportFile(cfs,tmpData.nodeid,directory,filename);
        }
    }
    return 1;
}

int CFS_ExportSource(CFS cfs,string source,string directory) {
    // Get source location
    string sourceBackup = copyString(source);
    location loc = getPathLocation(cfs->fileDesc,sourceBackup,cfs->currentDirectoryId,0);
    // Check if it exists
    if (loc.valid) {
        // Check source type (shortcuts are not exported)
        if (loc.type == TYPE_DIRECTORY) {
            // Directory
            CFS_ExportDirectory(cfs,loc.nodeid,directory);
        } else if (loc.type == TYPE_FILE) {
            // Regular file
            CFS_ExportFile(cfs,loc.nodeid,directory,loc.filenanme);
        }
    } else {
        printf("%s not found.\n",source);
    }
    DestroyString(&sourceBackup);
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
                                if (!CFS_CreateDirectory(cfs,loc.filenanme,loc.nodeid)) {
                                    printf("Not enough space to create directory %s\n",loc.filenanme);
                                }
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
                                    if (!CFS_CreateFile(cfs,loc.filenanme,loc.nodeid,"",0)) {
                                        printf("Not enough space to create file %s\n",loc.filenanme);
                                    }
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
                if (!lastword)
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
                            printf("Wrong option %s\n",option);
                            if (!lastword)
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
                                        if (loc.type == TYPE_DIRECTORY)
                                            CFS_ls(cfs->fileDesc,loc.nodeid,options,pathCopy);
                                        else
                                            CFS_PrintFileInfo(cfs->fileDesc,getMetadataFromNodeId(cfs->fileDesc,loc.nodeid),loc.filenanme,options);
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
                                    if (loc.type == TYPE_DIRECTORY)
                                        CFS_ls(cfs->fileDesc,loc.nodeid,options,pathCopy);
                                    else
                                        CFS_PrintFileInfo(cfs->fileDesc,getMetadataFromNodeId(cfs->fileDesc,loc.nodeid),loc.filenanme,options);
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
                if (!lastword)
                    IgnoreRemainingInput();
            }
        }
        // Merge multiple files to one
        else if (!strcmp("cfs_cat",commandLabel)) {
            // Check if we have an open file to work on
            if (cfs->fileDesc != -1) {
                // Usage check
                if (!lastword) {
                    // Read source files
                    Queue sourcesQueue;
                    Queue_Create(&sourcesQueue);
                    string source = readNextWord(&lastword);
                    unsigned int sources = 0;
                    while (!lastword && strcmp("-o",source)) {
                        Queue_Push(sourcesQueue,source);
                        DestroyString(&source);
                        source = readNextWord(&lastword);
                        sources++;
                    }
                    DestroyString(&source);
                    // Usage check
                    if (sources > 0) {
                        // Usage check:check if output file was specified
                        if (!lastword) {
                            string outputFile = readNextWord(&lastword);
                            // Usage check
                            if (lastword) {
                                location loc;
                                unsigned int totalSize = 0,ok = 1;
                                char datablocks[DATABLOCK_NUM];
                                string sourceBackup;
                                MDS sourceData;
                                // Concatinate all source files to output file
                                while (ok && !Queue_Empty(sourcesQueue)) {
                                    source = Queue_Pop(sourcesQueue);
                                    sourceBackup = copyString(source);
                                    // Get source location and check if it exists and is a regular file
                                    loc = getPathLocation(cfs->fileDesc,sourceBackup,cfs->currentDirectoryId,0);
                                    if (loc.valid) {
                                        if (loc.type == TYPE_FILE) {
                                            // Get source data
                                            sourceData = getMetadataFromNodeId(cfs->fileDesc,loc.nodeid);
                                            // Check if it fits in the curren concatinated file
                                            if (totalSize + sourceData.size <= cfs->MAX_FILE_SIZE) {
                                                memcpy(datablocks + totalSize,sourceData.data.datablocks,sourceData.size);
                                                totalSize += sourceData.size;
                                            } else {
                                                printf("%s cannot be concatinated to %s with the previous sources due to insufficient size in cfs.\n",source,outputFile);
                                                ok = 0;
                                            }
                                        } else {
                                            printf("%s not a file.\n",source);
                                            ok = 0;
                                        }
                                    } else {
                                        printf("File %s does not exist.\n",source);
                                        ok = 0;
                                    }
                                    DestroyString(&sourceBackup);
                                    DestroyString(&source);
                                }
                                // If there is enough space create the file
                                if (ok) {
                                    string outputFileCopy = copyString(outputFile);
                                    location outputFileLocation = getPathLocation(cfs->fileDesc,outputFileCopy,cfs->currentDirectoryId,1);
                                    // Check if output file location exists
                                    if (outputFileLocation.valid) {
                                        // Check if output file exists
                                        if (!exists(cfs->fileDesc,outputFileLocation.filenanme,outputFileLocation.nodeid)) {
                                            CFS_CreateFile(cfs,outputFileLocation.filenanme,outputFileLocation.nodeid,datablocks,totalSize);
                                        } else {
                                            printf("%s already exists.\n",outputFile);
                                        }
                                    } else {
                                        printf("Output directory does not exists.\n");
                                    }
                                    DestroyString(&outputFile);
                                }
                            } else {
                                printf("Usage:cfs_cat <SOURCE_FILES> -o <OUTPUT_FILE>\n");
                                IgnoreRemainingInput();
                            }
                            DestroyString(&outputFile);
                        } else {
                            printf("Usage:cfs_cat <SOURCE_FILES> -o <OUTPUT_FILE>\n");
                        }
                    } else {
                        printf("Usage:cfs_cat <SOURCE_FILES> -o <OUTPUT_FILE>\n");
                        if (!lastword)
                            IgnoreRemainingInput();
                    }
                    Queue_Destroy(&sourcesQueue);
                } else {
                    printf("Usage:cfs_cat <SOURCE_FILES> -o <OUTPUT_FILE>\n");
                }
            } else {
                printf("Not currently working with a cfs file.\n");
                if (!lastword)
                    IgnoreRemainingInput();
            }
        }
        // Create a hard link to a specific file
        else if (!strcmp("cfs_ln",commandLabel)) {
            // Check if we have an open file to work on
            if (cfs->fileDesc != -1) {
                // Usage check
                if (!lastword) {
                    // Read sourceFile
                    string sourceFile = readNextWord(&lastword);
                    string outputFile;
                    // Usage check
                    if (!lastword) {
                        // Read output file
                        outputFile = readNextWord(&lastword);
                        // Usage check
                        if (lastword) {
                            // Get source file location
                            location sourceLocation = getPathLocation(cfs->fileDesc,sourceFile,cfs->currentDirectoryId,0);
                            // Chech if the source file exists
                            if (sourceLocation.valid) {
                                if (sourceLocation.type == TYPE_FILE) {
                                    // Get output file location
                                    string outputFileCopy = copyString(outputFile);
                                    location outputLocation = getPathLocation(cfs->fileDesc,outputFileCopy,cfs->currentDirectoryId,1);
                                    // Check if output location exists
                                    if (outputLocation.valid) {
                                        // Check if a file with the same name exists in the output directory and create the hard link only if not
                                        if (!exists(cfs->fileDesc,outputLocation.filenanme,outputLocation.nodeid)) {
                                            if (!CFS_CreateHardLink(cfs,outputLocation.filenanme,sourceLocation.nodeid,outputLocation.nodeid)) {
                                                printf("Not enough space to create hardlink %s\n",outputLocation.filenanme);
                                            }
                                        } else {
                                            printf("A file with the same output file name already exists in that path.\n");
                                        }
                                    } else {
                                        printf("Specified output path does not exist.\n");
                                    }
                                    DestroyString(&outputFileCopy);
                                } else {
                                    printf("Specified source file is not a file.\n");
                                }
                            } else {
                                printf("Specified source file does not exist.\n");
                            }
                        } else {
                            printf("Usage:cfs_ln <SOURCE_FILE> <OUTPUT FILE>\n");
                            IgnoreRemainingInput();
                        }
                        DestroyString(&outputFile);
                    } else {
                        printf("Usage:cfs_ln <SOURCE_FILE> <OUTPUT FILE>\n");
                    }
                    DestroyString(&sourceFile);
                } else {
                    printf("Usage:cfs_ln <SOURCE_FILE> <OUTPUT FILE>\n");
                }
            } else {
                printf("Not currently working with a cfs file.\n");
                if (!lastword)
                    IgnoreRemainingInput();
            }
        }
        // Remove directory with it's contents(in depth 1 or recursively) or a single file(addtional option)
        else if (!strcmp("cfs_rm",commandLabel)) {
            // Check if we have an open file to work on
            if (cfs->fileDesc != -1) {
                // Usage check
                if (!lastword) {
                    // Read options
                    string option = readNextWord(&lastword);
                    int options[2] = {0,0};
                    unsigned int optionsCount = 0,ok = 1,lastwasoption = 0;
                    while (!lastword && ok && option[0] == '-') {
                        if (!strcmp("-i",option)) {
                            options[RM_PROMPT] = 1;
                        } else if (!strcmp("-r",option)) {
                            options[RM_RECURSIVE] = 1;
                        } else {
                            printf("Wrong option %s\n",option);
                            ok = 0;
                            if (!lastword)
                                IgnoreRemainingInput();
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
                                string destination = option,destinationCopy;
                                // Read directories
                                location loc;
                                while (1) {
                                    destinationCopy = copyString(destination);
                                    loc = getPathLocation(cfs->fileDesc,destinationCopy,cfs->currentDirectoryId,0);
                                    if (loc.valid) {
                                        if (loc.type == TYPE_DIRECTORY)
                                            CFS_RemoveDirectoryContent(cfs->fileDesc,loc.nodeid,options);
                                        else
                                            printf("%s not a directory.\n",destination);
                                    } else {
                                        printf("No such file or directory.\n");
                                    }
                                    DestroyString(&destinationCopy);
                                    DestroyString(&destination);
                                    if (!lastword) {
                                        destination = readNextWord(&lastword);
                                    } else {
                                        break;
                                    }
                                }
                            } else {
                                // Only options = wrong usage
                                printf("Usage:cfs_rm <OPTIONS> <DESTINATIONS>\n");
                            }
                        } else {
                            // Only directories were specified
                            string destination = option,destinationCopy;
                            // Read directories
                            location loc;
                            while (1) {
                                destinationCopy = copyString(destination);
                                loc = getPathLocation(cfs->fileDesc,destinationCopy,cfs->currentDirectoryId,0);
                                if (loc.valid) {
                                    if (loc.type == TYPE_DIRECTORY)
                                        CFS_RemoveDirectoryContent(cfs->fileDesc,loc.nodeid,options);
                                    else
                                        printf("%s not a directory.\n",destination);
                                } else {
                                    printf("No such file or directory.\n");
                                }
                                DestroyString(&destinationCopy);
                                DestroyString(&destination);
                                if (!lastword) {
                                    destination = readNextWord(&lastword);
                                } else {
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    printf("Usage:cfs_rm <OPTIONS> <DESTINATIONS>\n");
                }
            } else {
                printf("Not currently working with a cfs file.\n");
                if (!lastword)
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
                        printf("No such directory %s\n",directory);
                    }
                } else {
                    // Nothing was specified
                    printf("Usage:cfs_import <SOURCES> ... <DIRECTORY>\n");
                }
            } else {
                printf("Not currently working with a cfs file.\n");
                if (!lastword)
                    IgnoreRemainingInput();
            }
        }
        // Export cfs files/directories to linux fs
        else if (!strcmp("cfs_export",commandLabel)) {
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
                    // Check if directory exists
                    struct stat st;
                    if (stat(directory,&st) == 0 && S_ISDIR(st.st_mode)) {
                        // Read all sources from linux and import their contents in cfs
                        string source;
                        while (!Queue_Empty(sourcesQueue)) {
                            source = Queue_Pop(sourcesQueue);
                            CFS_ExportSource(cfs,source,directory);
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
                    printf("Usage:cfs_export <SOURCES> ... <DIRECTORY>\n");
                }
            } else {
                printf("Not currently working with a cfs file.\n");
                if (!lastword)
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
                int BLOCK_SIZE = sizeof(char),FILENAME_SIZE = MAX_FILENAME_SIZE,MAX_FILE_SIZE = DATABLOCK_NUM,MAX_DIRECTORY_FILE_NUMBER = sizeof(unsigned int) + MAX_FILENAME_SIZE*sizeof(char);
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
