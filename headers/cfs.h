#ifndef CFS_H
#define CFS_H

#include <time.h>

#define DATABLOCK_NUM 1000

typedef struct {
    unsigned  int  datablocks[DATABLOCK_NUM];
} Datastream;

typedef struct {
    unsigned int nodeid;
    char *filename;
    unsigned int size;
    unsigned int type;
    unsigned int parent_nodeid;
    time_t creation_time;
    time_t accessTime;
    time_t modificationTime;
    Datastream data;
} MDS;

#endif
