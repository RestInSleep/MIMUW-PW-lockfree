#include <malloc.h>
#include <pthread.h>
#include <stdatomic.h>

#include "SimpleQueue.h"

struct SimpleQueueNode;
typedef struct SimpleQueueNode SimpleQueueNode;

struct SimpleQueueNode {
    _Atomic(SimpleQueueNode*) next;
    Value item;
};

SimpleQueueNode* SimpleQueueNode_new(Value item)
{
    SimpleQueueNode* node = (SimpleQueueNode*)malloc(sizeof(SimpleQueueNode));
    atomic_init(&node->next, NULL);
    node->item = item;
    return node;
}

struct SimpleQueue {
    SimpleQueueNode* head;
    SimpleQueueNode* tail;
    pthread_mutex_t head_mtx;
    pthread_mutex_t tail_mtx;
};

SimpleQueue* SimpleQueue_new(void)
{

    SimpleQueue* queue = (SimpleQueue*)malloc(sizeof(SimpleQueue));
    queue->head =  SimpleQueueNode_new(TAKEN_VALUE);
    queue->tail = queue->head;
    pthread_mutex_init(&(queue->head_mtx), NULL);
    pthread_mutex_init(&(queue->tail_mtx), NULL);
    return queue;
}

void SimpleQueue_delete(SimpleQueue* queue)
{
    while(atomic_load(&(queue->head->next))) {
        SimpleQueueNode* temp = queue->head;
        queue->head = atomic_load(&queue->head->next);
        free(temp);
    }
    free(queue->head);
    free(queue);
}

void SimpleQueue_push(SimpleQueue* queue, Value item)
{

    SimpleQueueNode* new_tail = SimpleQueueNode_new(item);

    pthread_mutex_lock(&queue->tail_mtx);
    atomic_store(&queue->tail->next, new_tail);
    queue->tail = new_tail;

    pthread_mutex_unlock(&queue->tail_mtx);
}

Value SimpleQueue_pop(SimpleQueue* queue)
{
    Value result = EMPTY_VALUE;
    pthread_mutex_lock(&queue->head_mtx);

    SimpleQueueNode * new_head = atomic_load(&queue->head->next);
    if (new_head) {// if queue not empty
        result = new_head->item;
        SimpleQueueNode * old_head = queue->head;
        queue->head = new_head;
        free(old_head);
    }
    pthread_mutex_unlock(&queue->head_mtx);
    return result;
}

bool SimpleQueue_is_empty(SimpleQueue* queue)
{
    pthread_mutex_lock(&queue->head_mtx);
    bool result = false;
    if (!atomic_load(&queue->head->next)) {
        result = true;
    }
    pthread_mutex_unlock(&queue->head_mtx);
    return result;
}
