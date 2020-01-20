#ifndef MINHEAP_H
#define MINHEAP_H

#include "cfs.h"

typedef struct minheap *MinHeap;

MinHeap MinHeap_Create(int);
MDS MinHeap_FindMin(MinHeap);
int MinHeap_Insert(MinHeap,MDS);
MDS MinHeap_ExtractMin(MinHeap,int*);
int MinHeap_Destroy(MinHeap*);

#endif