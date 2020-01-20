#ifndef QUEUE_H
#define QUEUE_H

#include "string_functions.h"

typedef struct queue *Queue;

int Queue_Create(Queue*);
int Queue_Push(Queue,string);
string Queue_Pop(Queue);
int Queue_Empty(Queue);
int Queue_Destroy(Queue*);

#endif