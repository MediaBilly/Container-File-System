#ifndef CFS_H
#define CFS_H

#include <time.h>

typedef struct cfs *CFS;

int CFS_Init(CFS*);
int CFS_Run(CFS);
int CFS_Destroy(CFS*);

#endif
