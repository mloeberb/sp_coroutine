/**
 * @file test_multi_worker.c
 * @brief Test with multiple workers yielding multiple times
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 */

#include "sp_coroutine.h"
#include <stdio.h>

typedef struct {
    sp_co_pool_handle_t pool;
    int id;
    int iterations;
} worker_data_t;

void worker(void* arg) {
    worker_data_t* data = (worker_data_t*)arg;

    for (int i = 0; i < data->iterations; i++) {
        printf("Worker %d: iteration %d\n", data->id, i + 1);
        sp_co_yield(data->pool);
    }

    printf("Worker %d: completed\n", data->id);
}

void scheduler(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;

    worker_data_t data1 = {.pool = pool, .id = 1, .iterations = 3};
    worker_data_t data2 = {.pool = pool, .id = 2, .iterations = 4};
    worker_data_t data3 = {.pool = pool, .id = 3, .iterations = 2};

    sp_co_handle_t w1 = sp_co_add(pool, worker, &data1);
    sp_co_handle_t w2 = sp_co_add(pool, worker, &data2);
    sp_co_handle_t w3 = sp_co_add(pool, worker, &data3);

    printf("Scheduler: starting round-robin\n");

    // Run round-robin for 4 iterations (max of all workers)
    for (int round = 0; round < 4; round++) {
        printf("=== Round %d ===\n", round + 1);

        sp_co_state_t state;
        sp_co_state(w1, &state);
        if (state != SP_CO_STATE_DEAD) sp_co_go(pool, w1);

        sp_co_state(w2, &state);
        if (state != SP_CO_STATE_DEAD) sp_co_go(pool, w2);

        sp_co_state(w3, &state);
        if (state != SP_CO_STATE_DEAD) sp_co_go(pool, w3);
    }

    printf("Scheduler: done\n");
}

int main(void) {
    sp_co_pool_handle_t pool = sp_co_create(10, 64 * 1024);
    sp_co_handle_t sched = sp_co_add(pool, scheduler, pool);
    sp_co_start(pool, sched);
    sp_co_destroy(pool);
    return 0;
}
