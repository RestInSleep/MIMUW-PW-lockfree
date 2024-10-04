#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "BLQueue.h"
#include "HazardPointer.h"

struct BLNode;
typedef struct BLNode BLNode;
typedef _Atomic (BLNode *) AtomicBLNodePtr;

struct BLNode {
    AtomicBLNodePtr next;
    _Atomic Value buffer[BUFFER_SIZE];
    _Atomic uint64_t push_idx;
    _Atomic uint64_t pop_idx;
};


BLNode * BLNode_new(void) {

    BLNode* result = malloc(sizeof(BLNode));

    atomic_init(&result->next, NULL);

    for(int i = 0; i < BUFFER_SIZE; i++) {
        atomic_store(&result->buffer[i], EMPTY_VALUE);
    }

    atomic_init(&result->push_idx, 0);
    atomic_init(&result->pop_idx, 0);
    return result;
}

BLNode * BLNode_new2(Value item) {
    BLNode *result = malloc(sizeof(BLNode));
    atomic_init(&result->next, NULL);
    for(int i = 1; i < BUFFER_SIZE; i++) {
        atomic_store(&result->buffer[i], EMPTY_VALUE);
    }
    atomic_store(&result->buffer[0], item);
    atomic_init(&result->push_idx, 1);
    atomic_init(&result->pop_idx, 0);
    return result;
}


struct BLQueue {
    AtomicBLNodePtr head;
    AtomicBLNodePtr tail;
    HazardPointer* hp;
};

BLQueue *BLQueue_new(void) {
    BLQueue * queue = (BLQueue *) malloc(sizeof(BLQueue));
    BLNode * head = BLNode_new();
    atomic_init(&queue->head, head);
    atomic_init(&queue->tail, head);
    queue->hp = (HazardPointer*)malloc(sizeof(HazardPointer));
    HazardPointer_initialize(queue->hp);
    return queue;
}

void BLQueue_delete(BLQueue *queue) {
    BLNode * head = atomic_load(&(queue->head));
    HazardPointer_finalize(queue->hp); // delete every retired
    free(queue->hp); // delete hazard pointer structure

    while(atomic_load(&(atomic_load(&queue->head)->next))) {
        BLNode* temp = queue->head;
        atomic_store(&queue->head, atomic_load(&(atomic_load(&(queue->head))->next)));
        free(temp);
    }
    free(atomic_load(&queue->head));
    free(queue);
}

void BLQueue_push(BLQueue *queue, Value item) {
   bool success = false;
    while (!success) {
        BLNode* tail = HazardPointer_protect(queue->hp, (const _Atomic(void*)*)&queue->tail);
        // we secure the tail, so it can not be messed with (freed)
        BLNode * next = atomic_load(&tail->next);
        uint64_t push_idx = atomic_fetch_add(&(tail->push_idx), (uint64_t)1);

        _Atomic Value empty = EMPTY_VALUE;

        if (atomic_load(&push_idx) < BUFFER_SIZE) {
           if (atomic_compare_exchange_strong(&tail->buffer[push_idx], &empty, item)) {
               success = true;
           }
        }
        else {
            if (next) {
                atomic_compare_exchange_strong(&(queue->tail), &tail, next);
            }
            else {
                BLNode * node = BLNode_new2(item);
                BLNode * nil = NULL;
                 if (atomic_compare_exchange_strong(&tail->next, &nil, node)) {
                     atomic_compare_exchange_strong(&queue->tail, &tail, node);
                     success = true;
                 }
                 else {
                     free(node);
                 }
            }
        }
    }
    HazardPointer_clear(queue->hp);
}


Value BLQueue_pop(BLQueue *queue) {
   bool success = false;
   Value result;
   while (!success) {
       result = EMPTY_VALUE;
       BLNode * head = HazardPointer_protect(queue->hp, (const _Atomic (void *) *) &queue->head);
       BLNode * next = atomic_load(&head->next);
       uint64_t pop_idx = atomic_fetch_add(&(head->pop_idx), (uint64_t)1);

       if (pop_idx < BUFFER_SIZE) {
           result = atomic_exchange(&head->buffer[atomic_load(&pop_idx)], TAKEN_VALUE);
           if (result != EMPTY_VALUE && result != TAKEN_VALUE) {
                   success = true;
               }
       }
       else {
           if (next) {
              if (atomic_compare_exchange_strong(&queue->head, &head, next)) {
                  HazardPointer_retire(queue->hp, head);
              }
           }
           else {
               result = EMPTY_VALUE;
               success = true;
           }
       }
   }
   HazardPointer_clear(queue->hp);
   return result;
}

bool BLQueue_is_empty(BLQueue *queue) {
    bool result = false;
    BLNode * head = HazardPointer_protect(queue->hp, (const _Atomic (void *) *) &queue->head);
    if (!atomic_load(&head->next) &&(atomic_load(&head->pop_idx) >= atomic_load(&head->push_idx))) {
        result = true;
    }
    HazardPointer_clear(queue->hp);
    return result;
}
