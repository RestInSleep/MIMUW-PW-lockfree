#define rand rand_hook__
#include <stdio.h>
#include <threads.h>
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#undef rand
//Straszny hack, żeby wywalić randa.

#define SWAP(x, y, T) do { T SWAP = x; x = y; y = SWAP; } while (0)

#include "HazardPointer.h"

#include "SimpleQueue.h"
#include "RingsQueue.h"
#include "LLQueue.h"
#include "BLQueue.h"

struct QueueVTable {
    const char* name;
    void* (*new)(void);
    void (*push)(void* queue, Value item);
    Value (*pop)(void* queue);
    bool (*is_empty)(void* queue);
    void (*delete)(void* queue);
};
typedef struct QueueVTable QueueVTable;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"

const QueueVTable queueVTables[] = {
    { "SimpleQueue", SimpleQueue_new, SimpleQueue_push, SimpleQueue_pop, SimpleQueue_is_empty, SimpleQueue_delete },
    { "RingsQueue", RingsQueue_new, RingsQueue_push, RingsQueue_pop, RingsQueue_is_empty, RingsQueue_delete },
    { "LLQueue", LLQueue_new, LLQueue_push, LLQueue_pop, LLQueue_is_empty, LLQueue_delete },
    { "BLQueue", BLQueue_new, BLQueue_push, BLQueue_pop, BLQueue_is_empty, BLQueue_delete },
};

#pragma GCC diagnostic pop

int is_good(Value v) {
    return v != EMPTY_VALUE && v != TAKEN_VALUE;
}

struct xorwow_state {
    uint32_t x[5];
    uint32_t counter;
};

/* The state array must be initialized to not be all zero in the first four words */
uint32_t xorwow(struct xorwow_state *state)
{
    /* Algorithm "xorwow" from p. 5 of Marsaglia, "Xorshift RNGs" */
    uint32_t t  = state->x[4];

    uint32_t s  = state->x[0];  /* Perform a contrived 32-bit shift. */
    state->x[4] = state->x[3];
    state->x[3] = state->x[2];
    state->x[2] = state->x[1];
    state->x[1] = s;

    t ^= t >> 2;
    t ^= t << 1;
    t ^= s ^ (s << 4);
    state->x[0] = t;
    state->counter += 362437;
    return t + state->counter;
}

int64_t rand() {
    static bool initialized = false;
    static struct xorwow_state state;
    if(!initialized) {
        initialized = true;
        state.x[0] = 1943754;
        state.x[1] = 43287324;
        state.x[2] = 21312344;
        state.x[3] = 2131321;
        state.x[4] = 344444;
        state.counter = 1337;
    }
    int64_t hi = (int64_t)xorwow(&state),
            lo = (int64_t)xorwow(&state);
    int64_t result = (hi << 32) | lo;
    if(!is_good(result))
        return rand();
    return result;
}

void shuffle(Value *t, size_t size) {
    for(int _ = 0; _ < 2; _++) {
        for(size_t i = 1; i < size; i++) {
            size_t prev = ((size_t)rand()) % i;
            SWAP(t[i], t[prev], Value);
        }
    }
}

void basic_test(QueueVTable Q) {
    HazardPointer_register(0, 1);
    void *q = Q.new();

    const Value begin = 10, end = 30;

    for(Value i = begin; i <= end; i++)
        if(is_good(i))
            Q.push(q, i);

    for(Value i = begin; i <= end; i++) {
        if(is_good(i)) {
            assert(Q.pop(q) == i);
            assert(Q.is_empty(q) == (i == end));
        }
    }

    assert(Q.is_empty(q));

#define PUSH_ACTION 1
#define POP_ACTION 0

    const size_t count = 10000;
    int64_t *actions = malloc(2 * count * sizeof(int64_t));
    assert(actions != NULL);
    for(size_t i = 0; i < count; i++)
        actions[i] = PUSH_ACTION;
    for(size_t i = count; i < 2 * count; i++)
        actions[i] = POP_ACTION;

    shuffle(actions, 2 * count);

    {
        int depth = 0;
        int64_t modifier = 0;
        for(int i = 0; i < 2 * count; i++) {
            if(depth == 0 && (actions[i] ^ modifier) == POP_ACTION)
                modifier ^= 1;
            actions[i] ^= modifier;
            if(actions[i] == PUSH_ACTION)
                depth++;
            else
                depth--;
        }
    }

    Value push_iter = 12312321312;
    Value pop_iter = push_iter;

    for(size_t i = 0; i < 2 * count; i++) {
        if(actions[i] == PUSH_ACTION) {
            Q.push(q, push_iter++);
        }
        else {
            assert(Q.pop(q) == pop_iter++);
        }
        assert(Q.is_empty(q) == (push_iter == pop_iter));
    }

    free(actions);

#undef PUSH_ACTION
#undef POP_ACTION

    assert(Q.is_empty(q));

    Q.delete(q);

}

void small(QueueVTable Q) {
    HazardPointer_register(0, 1);
    void *q = Q.new();
    for(int j = 1; j <= 10; j++)
        Q.push(q, j);
    for(int j = 1; j <= 10; j++)
        Q.pop(q);
    Q.delete(q);
}

void leak_check(QueueVTable Q) {
    HazardPointer_register(0, 1);
    for(int i = 1; i < 1000; i++) {
        void *q = Q.new();
        for(int j = 1; j <= i; j++)
            Q.push(q, j);
        Q.delete(q);
    }

    for(int i = 1; i < 1000; i++) {
        void *q = Q.new();
        for(int j = 1; j <= i; j++)
            Q.push(q, j);
        for(int j = 1; j <= i / 3; j++)
            Q.pop(q);
        Q.delete(q);
    }

}

struct args_type;
typedef struct args_type args_type;

struct args_type {
    size_t count;
    size_t thrc;
    int id;
    void *queue;
    QueueVTable Q;
};

size_t test_size;
_Atomic(size_t) *is_used;
_Atomic(size_t) writes_left;
_Atomic(size_t) killswitch;
_Atomic(size_t) initialized;


void *writer(void *_args) {
    args_type args = *((args_type*)_args);
    QueueVTable Q = args.Q;
    size_t count = args.count;
    size_t thrc = args.thrc;
    int id = args.id;
    void *queue = args.queue;
    HazardPointer_register(id, thrc);

    atomic_fetch_add(&initialized, 1);
    while(atomic_load(&initialized) != thrc);


    free(_args);
    for(size_t i = 0; i < count; i++) {
        Q.push(queue, count * id + i + 1);
        atomic_fetch_sub(&writes_left, 1);
    }
    return 0;
}

void *reader(void *_args) {
    args_type args = *((args_type*)_args);
    QueueVTable Q = args.Q;
    void *queue = args.queue;
    size_t id = args.id;
    size_t thrc = args.thrc;
    free(_args);
    HazardPointer_register(id, thrc);

    atomic_fetch_add(&initialized, 1);
    while(atomic_load(&initialized) != thrc);

    Value val;
    while(true) {
        if(atomic_load(&writes_left) == 0) {
            val = Q.pop(queue);
            if(val == EMPTY_VALUE)
                break;
        }
        else
            val = Q.pop(queue);
        if(val == EMPTY_VALUE)
            continue;
        if(!(val > 0 && val <= test_size)) {
            printf("??? %ld\n", val);
            fflush(stdout);
        }
        assert(val > 0 && val <= test_size);
        atomic_fetch_add(&is_used[val], 1);
    }
    return 0;
}

void *spammer(void *_args) {
    args_type args = *((args_type*)_args);
    QueueVTable Q = args.Q;
    void *queue = args.queue;
    size_t thrc = args.thrc;
    size_t id = args.id;
    HazardPointer_register(id, thrc);

    atomic_fetch_add(&initialized, 1);
    while(atomic_load(&initialized) != thrc);

    free(_args);
    while(atomic_load(&killswitch) == 0) {
        Q.is_empty(queue);
    }
    return 0;
}

int max(int a, int b) {
    if(a < b)
        return b;
    return a;
}

void readwrite(int readers, int writers, int spammers, size_t count, QueueVTable Q) {
    test_size = writers * count;
    is_used = malloc((test_size + 1) * sizeof(_Atomic(size_t)));
    for(size_t i = 1; i <= test_size; i++)
        atomic_init(&is_used[i], 0);

    atomic_init(&writes_left, test_size);
    atomic_init(&killswitch, 0);
    atomic_init(&initialized, 0);


    // Te +1 są po to żeby nie było problemów z rozmiarem 0
    pthread_t write_thread[writers + 1];
    pthread_t read_thread[readers + 1];
    pthread_t spam_thread[spammers + 1];
    //

    void *queue = Q.new();
    for(int i = 0; i < readers; ++i) {
        args_type* arg = malloc(sizeof(args_type));
        arg->count = count;
        arg->id = writers + i;
        arg->queue = queue;
        arg->Q = Q;
        arg->thrc = readers + writers + spammers + 1;
        pthread_create(&read_thread[i], NULL, reader, (void*)arg);
    }
    for(int i = 0; i < writers; ++i) {
        args_type* arg = malloc(sizeof(args_type));
        arg->count = count;
        arg->id = i;
        arg->queue = queue;
        arg->Q = Q;
        arg->thrc = readers + writers + spammers + 1;
        assert(pthread_create(&write_thread[i], NULL, writer, (void*)arg) == 0);
    }
    for(int i = 0; i < spammers; ++i) {
        args_type* arg = malloc(sizeof(args_type));
        arg->count = count;
        arg->id = i + readers + writers;
        arg->queue = queue;
        arg->Q = Q;
        arg->thrc = readers + writers + spammers + 1;
        assert(pthread_create(&spam_thread[i], NULL, spammer, (void*)arg) == 0);
    }

    HazardPointer_register(readers + writers + spammers, readers + writers + spammers + 1);
    atomic_fetch_add(&initialized, 1);

    for(int i = 0; i < readers; i++)
        pthread_join(read_thread[i], NULL);
    for(int i = 0; i < writers; i++)
        pthread_join(write_thread[i], NULL);

    atomic_store(&killswitch, 1);

    for(int i = 0; i < spammers; i++)
        pthread_join(spam_thread[i], NULL);

    for(size_t i = 1; i <= test_size; i++) {
        size_t val = atomic_load(&is_used[i]);
        if(val != 1) {
            printf("%lu, %lu\n", i, val);
        }
        assert(val == 1);
    }

    assert(atomic_load(&writes_left) == 0);
    free(is_used);
    Q.delete(queue);
}

int main(void)
{

    for (int i = 0; i < sizeof(queueVTables) / sizeof(QueueVTable); ++i) {
        QueueVTable Q = queueVTables[i];
        printf("Queue type: %s\n", Q.name);
        // Jeden wątek
        small(Q);
        basic_test(Q);
        leak_check(Q);
        // ----

        //readwrite(1, 1, 1, 77000, Q);

        // Więcej wątków

        for(int spammers = 0; spammers <= 3; spammers += 1) {
            for(int readers = 1; readers <= 7; readers++) {
                for(int writers = 1; writers <= 7; writers++) {
                    printf("Running %d readers, %d writers, %d spammers\n", readers, writers, spammers);
                    readwrite(readers, writers, spammers, 10000, Q);
                }
            }
        }

        printf("RUNNING BIG\n");
        readwrite(30, 30, 30, 7312, Q);
    }

    printf("AC :>\n");

    return 0;
}
