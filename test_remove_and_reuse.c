/**
 * @file test_remove_and_reuse.c
 * @brief Verify a DEAD coroutine slot can be removed and reused
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 */

#include "sp_coroutine.h"
#include <stdio.h>

static const char* failure;
static size_t observed_count;
static size_t expected_count;

static void short_lived(void* arg) {
    (void)arg;
}

static void scheduler(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;

    sp_co_handle_t a = sp_co_add(pool, short_lived, NULL);
    if (!a) { failure = "first add returned NULL"; return; }

    expected_count = 2;
    observed_count = sp_co_pool_count(pool);
    if (observed_count != expected_count) { failure = "count after first add"; return; }

    sp_co_go(pool, a);

    sp_co_state_t state;
    sp_co_state(a, &state);
    if (state != SP_CO_STATE_DEAD) { failure = "coroutine did not reach DEAD"; return; }

    if (sp_co_remove(pool, a) != SP_CO_OK) { failure = "remove failed"; return; }

    expected_count = 1;
    observed_count = sp_co_pool_count(pool);
    if (observed_count != expected_count) { failure = "count after remove"; return; }

    sp_co_handle_t b = sp_co_add(pool, short_lived, NULL);
    if (!b) { failure = "re-add returned NULL"; return; }

    expected_count = 2;
    observed_count = sp_co_pool_count(pool);
    if (observed_count != expected_count) { failure = "count after re-add"; return; }
}

int main(void) {
    sp_co_pool_handle_t pool = sp_co_create(5, 32 * 1024);
    if (sp_co_pool_capacity(pool) != 5) {
        printf("FAIL: capacity expected 5, got %zu\n", sp_co_pool_capacity(pool));
        sp_co_destroy(pool);
        return 1;
    }
    sp_co_handle_t s = sp_co_add(pool, scheduler, pool);
    sp_co_start(pool, s);
    sp_co_destroy(pool);

    if (!failure) {
        printf("Remove-and-reuse OK: slot freed and re-added\n");
        return 0;
    }
    printf("FAIL: %s (expected %zu, got %zu)\n",
           failure, expected_count, observed_count);
    return 1;
}
