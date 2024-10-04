#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "HazardPointer.h"
#include "LLQueue.h"

struct LLNode;
typedef struct LLNode LLNode;
typedef _Atomic(LLNode*) AtomicLLNodePtr;

struct LLNode {
    AtomicLLNodePtr next;
    _Atomic Value item;
};

LLNode* LLNode_new(Value item)
{
    LLNode* node = (LLNode*)malloc(sizeof(LLNode));
    atomic_store(&node->item , item);
    atomic_init(&node->next, NULL);
    return node;
}

struct LLQueue {
    AtomicLLNodePtr head;
    AtomicLLNodePtr tail;
    HazardPointer* hp;
};

LLQueue* LLQueue_new(void)
{
    LLQueue* queue = (LLQueue*)malloc(sizeof(LLQueue));
    LLNode * head = LLNode_new(EMPTY_VALUE);
    atomic_store(&queue->head, head);
    atomic_store(&queue->tail, head);

    queue->hp = (HazardPointer*)malloc(sizeof(HazardPointer));
    HazardPointer_initialize(queue->hp);
    return queue;
}

void LLQueue_delete(LLQueue* queue)
{
    HazardPointer_finalize(queue->hp); // delete every retired
    free(queue->hp); // delete hazard pointer structure
    while(atomic_load(&(atomic_load(&queue->head)->next))) {
        LLNode* temp = queue->head;
        atomic_store(&queue->head, atomic_load(&(atomic_load(&queue->head)->next)));
        free(temp);
    }
    free(atomic_load(&queue->head));
    free(queue);
}

void LLQueue_push(LLQueue* queue, Value item)
{
    LLNode * node = LLNode_new(item);
    bool success = false;
    while (!success) {
        LLNode* tail = HazardPointer_protect(queue->hp, (const _Atomic(void*)*)&queue->tail);
        LLNode* next = atomic_load(&tail->next);
        LLNode* nil = NULL;

        if (atomic_compare_exchange_strong(&tail->next, &nil, node)) {
            success = true;
            atomic_compare_exchange_strong(&queue->tail, &tail, node);
            HazardPointer_clear(queue->hp);
        }
        else {
            if (next) {
                atomic_compare_exchange_strong(&queue->tail, &tail, next);
            }
        }
    }
}

Value LLQueue_pop(LLQueue* queue) {
    while (true) {
        LLNode * head = HazardPointer_protect(queue->hp, (const _Atomic (void *) *) &queue->head);
        LLNode * next = head->next;
        Value val = atomic_load(&head->item);

        if (val != EMPTY_VALUE) {
            if(atomic_compare_exchange_strong(&head->item, &val, EMPTY_VALUE)) {
                // if we succeeded in popping
                if (next) { // if head can be retired
                    if (atomic_compare_exchange_strong(&queue->head, &head, next)) {
                        HazardPointer_retire(queue->hp, head);
                    }
                }
                HazardPointer_clear(queue->hp);
                return val;
            }
        }
        else { //head has empty value
            if (next == NULL) {
                HazardPointer_clear(queue->hp);
                return EMPTY_VALUE;
            }
            else {
                if (atomic_compare_exchange_strong(&queue->head, &head, next)) {
                    HazardPointer_retire(queue->hp, head);
                }
            }
        }
    }
}

bool LLQueue_is_empty(LLQueue* queue)
{
   bool result = false;
    LLNode * head = HazardPointer_protect(queue->hp, (const _Atomic (void *)*) &queue->head);
    LLNode * next = atomic_load(&head->next);
    if (!next && head->item == EMPTY_VALUE) {
        result = true;
    }
    HazardPointer_clear(queue->hp);
    return result;
}
