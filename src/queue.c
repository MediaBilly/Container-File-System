#include <stdio.h>
#include <stdlib.h>
#include "../headers/queue.h"

typedef struct qnode *QueueNode;

struct queue
{
  unsigned int count;
  QueueNode front;
};

struct qnode {
  string element;
  QueueNode next;
};

int Queue_Create(Queue *q) {
  // Allocate memory for queue
  if ((*q = (Queue)malloc(sizeof(struct queue))) == NULL) {
    printf("Not enough memory.\n");
    return 0;
  }
  // Initialize attributes
  (*q)->count = 0;
  (*q)->front = NULL;
  return 1;
}

int Queue_Push(Queue q,string element) {
  // Check if queue was previously inititialized
  if (q != NULL) {
    // Keep current front in a temporary variable
    QueueNode tmp = q->front;
    // Allocate memory for new element
    if ((q->front = (QueueNode)malloc(sizeof(struct qnode))) == NULL) {
      printf("Not enough memory.\n");
      return 0;
    }
    q->front->element = copyString(element);
    q->front->next = tmp;
    q->count++;
    return 1;
  } else {
    printf("Queue not yet initialized.\n");
    return 0;
  }
}

string Queue_Pop(Queue q) {
  // Check if queue was previously inititialized
  if (q != NULL) {
    // Check if queue is not empty
    if (q->count > 0) {
      // Delete the top node
      QueueNode toDel = q->front;
      q->front = q->front->next;
      string ret = toDel->element;
      free(toDel);
      q->count--;
      // Return it's value
      return ret;
    } else {
      printf("Queue is empty.\n");
      return NULL;
    }
  } else {
    printf("Queue not yet initialized.\n");
    return NULL;
  }
}

int Queue_Empty(Queue q) {
  return q != NULL ? !q->count : 0;
}

int Queue_Destroy(Queue *q) {
  // Check if queue was previously inititialized
  if (*q != NULL) {
    // Delete all the elements
    while (!Queue_Empty(*q)) {
      free(Queue_Pop(*q));
    }
    // Free memory allocated for the queue structure
    free(*q);
    *q = NULL;
    return 1;
  } else {
    return 0;
  }
}