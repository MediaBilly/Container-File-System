#ifndef CFS_H
#define CFS_H

#include <time.h>

typedef struct cfs *CFS;

// File definitions
#define DATABLOCK_NUM 1000
#define FILE_PERMISSIONS 0644
#define MAX_FILENAME_SIZE 50

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

int CFS_Init(CFS*);
int CFS_Run(CFS);
int CFS_Destroy(CFS*);

#endif
