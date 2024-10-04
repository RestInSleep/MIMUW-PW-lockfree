#include <malloc.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include "HazardPointer.h"

thread_local int _thread_id = -1;
int _num_threads = -1;



void HazardPointer_register(int thread_id, int num_threads)
{
    if(!thread_id) {
        _num_threads = num_threads;
    }
    _thread_id = thread_id;
}

void HazardPointer_initialize(HazardPointer* hp)
{
    for (int i = 0; i < MAX_THREADS; i++) {
        atomic_store(&hp->pointer[i], NULL); // every reserved is NULL
        hp->retirement_count[i] = 0;
        for (int j = 0; j < MAX_THREADS; j++) { // there are no retirements yet
            hp->retirements[i][j] = NULL;
        }
    }
}

void HazardPointer_finalize(HazardPointer* hp)
{
    for (int i = 0; i < _num_threads; i++) { // for every thread
        atomic_store(&hp->pointer[i], NULL); // clear every reserved address
        for (int j = 0; j < MAX_THREADS; j++) { // free every retired node
            if (hp->retirements[i][j]) { // if not null:
                free(hp->retirements[i][j]);
                hp->retirements[i][j] =  NULL;
            }
        }
    }
}

void* HazardPointer_protect(HazardPointer* hp, const _Atomic(void*)* atom) {
   bool protected = false;
   void* ptr = NULL;
   while(!protected) {
       ptr = atomic_load(atom);
       atomic_store(&hp->pointer[_thread_id], ptr);
       if (atomic_load(atom) == ptr){
           protected = true;
       }
   }
    return ptr;
}

void HazardPointer_clear(HazardPointer* hp)
{
    atomic_store(&hp->pointer[_thread_id], NULL);
}

void HazardPointer_retire(HazardPointer* hp, void* ptr)
{
   while (hp->retirement_count[_thread_id] == RETIRED_THRESHOLD) {
       for (int i = 0; i < RETIRED_THRESHOLD; i++) {
           void* toRetire = hp->retirements[_thread_id][i];
           bool can_be_freed = true;

           for(int j = 0; j < _num_threads; j++) {
               if (atomic_load(&hp->pointer[j]) == toRetire) {
                   can_be_freed = false;
                   j = _num_threads;
               }
           }
           if (can_be_freed) {
               free(toRetire);
               hp->retirements[_thread_id][i] = NULL;
               hp->retirement_count[_thread_id]--;
           }
       }
   }
   for (int i = 0; i < RETIRED_THRESHOLD; i++) {
       if(!hp->retirements[_thread_id][i]) { // if we found free space
           hp->retirements[_thread_id][i] = ptr;
           hp->retirement_count[_thread_id]++;
           return;
       }
   }
}
