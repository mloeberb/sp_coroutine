/**
 * @file test_yield_from_main.c
 * @brief Verify the main coroutine cannot yield
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 */

#include "sp_coroutine.h"
#include <stdio.h>

static sp_co_result_t yield_result;

static void main_coro(void* arg) {
    sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
    yield_result = sp_co_yield(pool);
}

int main(void) {
    sp_co_pool_handle_t pool = sp_co_create(5, 32 * 1024);
    sp_co_handle_t main_co = sp_co_add(pool, main_coro, pool);
    sp_co_start(pool, main_co);
    sp_co_destroy(pool);

    if (yield_result == SP_CO_ERR_YIELD_FROM_MAIN) {
        printf("Main yield rejected as expected (SP_CO_ERR_YIELD_FROM_MAIN)\n");
        return 0;
    }
    printf("FAIL: sp_co_yield returned %d, expected %d\n",
           yield_result, SP_CO_ERR_YIELD_FROM_MAIN);
    return 1;
}
