#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>

#include <stdio.h>
#include <stdlib.h>

#include "HazardPointer.h"
#include "RingsQueue.h"

struct RingsQueueNode;
typedef struct RingsQueueNode RingsQueueNode;

struct RingsQueueNode {
    _Atomic (RingsQueueNode *) next;
    Value buffer[RING_SIZE];
    _Atomic (uint64_t) push_idx;
    _Atomic (uint64_t) pop_idx;

};

// TODO RingsQueueNode_new
RingsQueueNode *RingsQueueNode_new(void) {
    RingsQueueNode *node = (RingsQueueNode *) malloc(sizeof(RingsQueueNode));
    atomic_init(&node->next, NULL);
    atomic_init(&node->pop_idx, 0);
    atomic_init(&node->push_idx, 0);
    return node;
}

// TODO RingsQueueNode_new
RingsQueueNode *RingsQueueNode_new2(Value item) {
    RingsQueueNode *node = (RingsQueueNode *) malloc(sizeof(RingsQueueNode));
    atomic_init(&node->next, NULL);
    atomic_init(&node->pop_idx, 0);
    atomic_init(&node->push_idx, 1);
    node->buffer[0] = item;
    return node;
}

struct RingsQueue {
    RingsQueueNode *head;
    RingsQueueNode *tail;
    pthread_mutex_t pop_mtx;
    pthread_mutex_t push_mtx;
};

RingsQueue *RingsQueue_new(void) {
    RingsQueue *queue = (RingsQueue *) malloc(sizeof(RingsQueue));
    queue->head = RingsQueueNode_new();
    queue->tail = queue->head;
    pthread_mutex_init(&(queue->pop_mtx), NULL);
    pthread_mutex_init(&(queue->push_mtx), NULL);
    return queue;
}


void RingsQueue_delete(RingsQueue *queue) {
    while (atomic_load(&queue->head->next)) {
        RingsQueueNode *temp = queue->head;
        queue->head = atomic_load(&queue->head->next);
        free(temp);
    }
    free(queue->head);
    pthread_mutex_destroy(&queue->pop_mtx);
    pthread_mutex_destroy(&queue->push_mtx);
    free(queue);
}

void RingsQueue_push(RingsQueue *queue, Value item) {

    pthread_mutex_lock(&queue->push_mtx);

    uint64_t push_idx = atomic_load(&(queue->tail->push_idx));
    if (push_idx - atomic_load(&queue->tail->pop_idx) == RING_SIZE) { // the ring is full
        RingsQueueNode *node = RingsQueueNode_new2(item);
        atomic_store(&queue->tail->next, node);
        queue->tail = atomic_load(&queue->tail->next);
        pthread_mutex_unlock(&queue->push_mtx);
        return;
    }
    queue->tail->buffer[push_idx % RING_SIZE] = item;
    atomic_store(&(queue->tail->push_idx), push_idx + 1);

    pthread_mutex_unlock(&queue->push_mtx);
}



Value RingsQueue_pop(RingsQueue * queue) {
    Value result = EMPTY_VALUE;
    pthread_mutex_lock(&(queue->pop_mtx));
    uint64_t pop_idx = atomic_load(&queue->head->pop_idx);
    if (atomic_load(&queue->head->next)) { // the head is not the only node
        if (atomic_load(&queue->head->push_idx) == pop_idx) {
            // the head is empty and there is successor
            RingsQueueNode * temp = queue->head;
            queue->head = queue->head->next;
            free(temp);
            pop_idx = atomic_load(&queue->head->pop_idx);
        }
            result = queue->head->buffer[pop_idx % RING_SIZE];
            atomic_store(&queue->head->pop_idx, pop_idx + 1);
    }
    else {
        if (atomic_load(&queue->head->push_idx) > pop_idx) {
            result = queue->head->buffer[ pop_idx % RING_SIZE];
            atomic_store(&queue->head->pop_idx, pop_idx + 1);
        }
    }
    pthread_mutex_unlock(&(queue->pop_mtx));
    return result;
}


bool RingsQueue_is_empty(RingsQueue *queue) {
    bool result = false;
    pthread_mutex_lock(&queue->pop_mtx);

    int pop_count = atomic_load(&queue->head->pop_idx);
    int push_count = atomic_load(&queue->head->push_idx);

    if (push_count - pop_count == 0) {
        if (!atomic_load(&queue->head->next))
            result = true;
    }
    pthread_mutex_unlock(&queue->pop_mtx);
    return result;
}
