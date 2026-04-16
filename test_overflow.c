/**
 * @file test_overflow.c
 * @brief Verify that stack overflow is detected via the sentinel mechanism
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 */

#include "sp_coroutine.h"
#include <stdio.h>

static sp_co_result_t go_result;

static void stack_hog(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    // Allocate and densely write a buffer larger than the coroutine's
    // stack_size budget to force the sentinel to be corrupted.
    volatile char big[32 * 1024];
    for (size_t i = 0; i < sizeof(big); i++) {
        big[i] = (char)(i & 0xFF);
    }
    sp_co_yield(pool);
}

static void scheduler(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    sp_co_handle_t h = sp_co_add(pool, stack_hog, pool);
    go_result = sp_co_go(pool, h);
}

int main(void) {
    sp_co_pool_handle_t pool = sp_co_create(5, 16 * 1024);  // 16KB budget
    sp_co_handle_t sched = sp_co_add(pool, scheduler, pool);
    sp_co_start(pool, sched);
    sp_co_destroy(pool);

    if (go_result == SP_CO_ERR_STACK_OVERFLOW) {
        printf("Overflow detected as expected (SP_CO_ERR_STACK_OVERFLOW)\n");
        return 0;
    }
    printf("FAIL: sp_co_go returned %d, expected %d\n",
           go_result, SP_CO_ERR_STACK_OVERFLOW);
    return 1;
}
