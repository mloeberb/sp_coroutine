/**
 * @file test_state_transitions.c
 * @brief Verify coroutine state transitions: READY -> RUNNING -> SUSPENDED -> DEAD
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 */

#include "sp_coroutine.h"
#include <stdio.h>

static const char* failure;
static int observed;
static int expected;

static int check_state(sp_co_handle_t co, sp_co_state_t want, const char* label) {
    sp_co_state_t got;
    sp_co_state(co, &got);
    if (got != want) {
        failure = label;
        observed = (int)got;
        expected = (int)want;
        return 0;
    }
    return 1;
}

static void worker(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    sp_co_handle_t self = sp_co_current(pool);
    if (!check_state(self, SP_CO_STATE_RUNNING, "worker entry")) return;
    sp_co_yield(pool);
    if (!check_state(self, SP_CO_STATE_RUNNING, "worker after yield")) return;
}

static void scheduler(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    sp_co_handle_t w = sp_co_add(pool, worker, pool);
    if (!check_state(w, SP_CO_STATE_READY, "after add")) return;
    sp_co_go(pool, w);
    if (!check_state(w, SP_CO_STATE_SUSPENDED, "after first go")) return;
    sp_co_go(pool, w);
    if (!check_state(w, SP_CO_STATE_DEAD, "after second go")) return;
}

int main(void) {
    sp_co_pool_handle_t pool = sp_co_create(5, 32 * 1024);
    sp_co_handle_t s = sp_co_add(pool, scheduler, pool);
    sp_co_start(pool, s);
    sp_co_destroy(pool);

    if (!failure) {
        printf("State transitions OK: READY -> RUNNING -> SUSPENDED -> DEAD\n");
        return 0;
    }
    printf("FAIL: %s (expected state %d, got %d)\n", failure, expected, observed);
    return 1;
}
