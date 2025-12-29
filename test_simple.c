/**
 * @file test_simple.c
 * @brief Very simple test
 */

#include "sp_coroutine.h"
#include <stdio.h>

void worker(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    printf("Worker: before yield\n");
    sp_co_yield(pool);
    printf("Worker: after yield\n");
}

void scheduler(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    
    sp_co_handle_t w = sp_co_add(pool, worker, pool);
    printf("Sched: first go\n");
    sp_co_go(pool, w);
    printf("Sched: second go\n");
    sp_co_go(pool, w);
    printf("Sched: done\n");
}

int main(void) {
    sp_co_pool_handle_t pool = sp_co_create(5, 64 * 1024);
    sp_co_handle_t sched = sp_co_add(pool, scheduler, pool);
    sp_co_start(pool, sched);
    sp_co_destroy(pool);
    return 0;
}
