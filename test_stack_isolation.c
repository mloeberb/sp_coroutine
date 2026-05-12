/**
 * @file test_stack_isolation.c
 * @brief Verify per-coroutine stack regions don't clobber each other
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 *
 * Three coroutines each fill an in-stack buffer with a distinct byte
 * (1, 2, 3) and yield. On their second activation each coroutine verifies
 * its buffer still holds its own pattern. This demonstrates that yielding
 * and resuming siblings does not corrupt a coroutine's stack data.
 */

#include "sp_coroutine.h"
#include <stdio.h>

#define BUF_SIZE (60 * 1024)

typedef struct {
    sp_co_pool_handle_t pool;
    char value;
    int corrupted;
} worker_arg_t;

static void worker(void* arg) {
    worker_arg_t* a = (worker_arg_t*)arg;
    volatile char buf[BUF_SIZE];

    for (size_t i = 0; i < BUF_SIZE; i++) {
        buf[i] = a->value;
    }
    sp_co_yield(a->pool);

    for (size_t i = 0; i < BUF_SIZE; i++) {
        if (buf[i] != a->value) {
            a->corrupted = 1;
            break;
        }
    }
}

static worker_arg_t a1, a2, a3;

static void scheduler(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    a1.pool = pool; a1.value = 1; a1.corrupted = 0;
    a2.pool = pool; a2.value = 2; a2.corrupted = 0;
    a3.pool = pool; a3.value = 3; a3.corrupted = 0;

    sp_co_handle_t c1 = sp_co_add(pool, worker, &a1);
    sp_co_handle_t c2 = sp_co_add(pool, worker, &a2);
    sp_co_handle_t c3 = sp_co_add(pool, worker, &a3);

    sp_co_go(pool, c1);
    sp_co_go(pool, c2);
    sp_co_go(pool, c3);

    sp_co_go(pool, c1);
    sp_co_go(pool, c2);
    sp_co_go(pool, c3);
}

int main(void) {
    sp_co_pool_handle_t pool = sp_co_create(5, 64 * 1024);
    sp_co_handle_t sched = sp_co_add(pool, scheduler, pool);
    sp_co_start(pool, sched);
    sp_co_destroy(pool);

    if (!a1.corrupted && !a2.corrupted && !a3.corrupted) {
        printf("Stack isolation OK: each coroutine's buffer preserved\n");
        return 0;
    }
    printf("FAIL: corruption detected (c1=%d c2=%d c3=%d)\n",
           a1.corrupted, a2.corrupted, a3.corrupted);
    return 1;
}
