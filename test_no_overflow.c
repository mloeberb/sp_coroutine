/**
 * @file test_no_overflow.c
 * @brief Verify the stack sentinel does not fire when usage stays within budget
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 */

#include "sp_coroutine.h"
#include <stdio.h>

static sp_co_result_t go_result;

static void within_budget(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    // Densely write a buffer sized close to (but below) the coroutine's
    // stack_size budget, leaving only a small prologue allowance. The
    // sentinel must remain intact and sp_co_go must return OK.
    volatile char buf[28 * 1024];
    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (char)(i & 0xFF);
    }
    sp_co_yield(pool);
}

static void scheduler(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    sp_co_handle_t h = sp_co_add(pool, within_budget, pool);
    go_result = sp_co_go(pool, h);
}

int main(void) {
    sp_co_pool_handle_t pool = sp_co_create(5, 32 * 1024);
    sp_co_handle_t sched = sp_co_add(pool, scheduler, pool);
    sp_co_start(pool, sched);
    sp_co_destroy(pool);

    if (go_result == SP_CO_OK) {
        printf("Sentinel intact as expected (SP_CO_OK)\n");
        return 0;
    }
    printf("FAIL: sp_co_go returned %d, expected %d\n",
           go_result, SP_CO_OK);
    return 1;
}
