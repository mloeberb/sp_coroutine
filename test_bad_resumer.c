/**
 * @file test_bad_resumer.c
 * @brief Verify that resuming a SUSPENDED coroutine from a non-original
 *        caller is detected via SP_CO_ERR_BAD_RESUMER.
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 */

#include "sp_coroutine.h"
#include <stdio.h>

static sp_co_handle_t g_child;
static sp_co_result_t g_bad_resume_result;

static void child(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    sp_co_yield(pool);
}

static void coroutine_a(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    g_child = sp_co_add(pool, child, pool);
    sp_co_go(pool, g_child);   // first activation; child yields back, caller = A
    sp_co_yield(pool);         // yield back to main, leaving child SUSPENDED
}

static void coroutine_b(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    // Original caller of g_child was A. B is not A, so this must fail.
    g_bad_resume_result = sp_co_go(pool, g_child);
}

static void scheduler(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    sp_co_handle_t a = sp_co_add(pool, coroutine_a, pool);
    sp_co_handle_t b = sp_co_add(pool, coroutine_b, pool);
    sp_co_go(pool, a);
    sp_co_go(pool, b);
}

int main(void) {
    sp_co_pool_handle_t pool = sp_co_create(5, 32 * 1024);
    sp_co_handle_t sched = sp_co_add(pool, scheduler, pool);
    sp_co_start(pool, sched);
    sp_co_destroy(pool);

    if (g_bad_resume_result == SP_CO_ERR_BAD_RESUMER) {
        printf("Cross-caller resume detected as expected (SP_CO_ERR_BAD_RESUMER)\n");
        return 0;
    }
    printf("FAIL: sp_co_go returned %d, expected %d\n",
           g_bad_resume_result, SP_CO_ERR_BAD_RESUMER);
    return 1;
}
