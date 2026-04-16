/**
 * @file sp_coroutine.h
 * @brief Simple asymmetric coroutine library using setjmp/longjmp
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 * 
 * This library provides cooperative multitasking with asymmetric coroutines.
 * Coroutines yield control back to their caller, creating a hierarchical
 * activation model.
 * 
 * @example Basic Usage
 * @code
 * void worker(void* arg) {
 *     sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
 *     for (int i = 0; i < 3; i++) {
 *         printf("Worker iteration %d\n", i);
 *         sp_co_yield(pool);
 *     }
 * }
 *
 * void scheduler(void* arg) {
 *     sp_co_pool_handle_t pool = (sp_co_pool_handle_t)arg;
 *     sp_co_handle_t co = sp_co_add(pool, worker, pool);
 *
 *     for (int i = 0; i < 3; i++) {
 *         printf("Scheduler activating worker\n");
 *         sp_co_go(pool, co);
 *     }
 * }
 * 
 * int main(void) {
 *     sp_co_pool_handle_t pool = sp_co_create(10, 64 * 1024);
 *     if (!pool) return 1;
 *     
 *     sp_co_handle_t main_co = sp_co_add(pool, scheduler, pool);
 *     sp_co_result_t ret = sp_co_start(pool, main_co);
 *
 *     sp_co_destroy(pool);
 *     return ret;
 * }
 * @endcode
 */

#ifndef SP_COROUTINE_H
#define SP_COROUTINE_H

#include <stddef.h>

/**
 * @brief Opaque handle to a coroutine pool
 */
typedef struct sp_co_pool* sp_co_pool_handle_t;

/**
 * @brief Opaque handle to a coroutine
 */
typedef struct sp_coroutine* sp_co_handle_t;

/**
 * @brief Coroutine function signature
 * @param arg User-provided argument passed to the coroutine
 */
typedef void (*sp_co_func_t)(void* arg);

/**
 * @brief Error codes returned by library functions
 */
typedef enum {
    SP_CO_OK = 0,                    /**< Operation succeeded */
    SP_CO_ERR_INVALID = -1,          /**< Invalid parameter (NULL handle) */
    SP_CO_ERR_STATE = -2,            /**< Invalid coroutine state for operation */
    SP_CO_ERR_YIELD_FROM_MAIN = -3,  /**< Attempted yield from main coroutine */
    SP_CO_ERR_STACK_OVERFLOW = -4    /**< Coroutine exceeded its stack budget */
} sp_co_result_t;

/**
 * @brief Coroutine states
 */
typedef enum {
    SP_CO_STATE_READY = 0,      /**< Coroutine ready to run (not yet started) */
    SP_CO_STATE_RUNNING = 1,    /**< Coroutine currently executing */
    SP_CO_STATE_SUSPENDED = 2,  /**< Coroutine suspended (yielded) */
    SP_CO_STATE_DEAD = 3        /**< Coroutine finished execution */
} sp_co_state_t;

/**
 * @brief Create a new coroutine pool
 * 
 * Allocates and initializes a coroutine pool with fixed capacity and
 * uniform stack size for all coroutines. Stack size must be between
 * 16KB and 8MB (validated at runtime).
 * 
 * @param max_coroutines Maximum number of coroutines in the pool
 * @param stack_size Stack size in bytes for each coroutine (16KB - 8MB)
 * @return Pool handle on success, NULL on failure (invalid stack size or allocation error)
 * 
 * @note Pool is single-use. After sp_co_start returns, pool cannot be restarted.
 */
sp_co_pool_handle_t sp_co_create(size_t max_coroutines, size_t stack_size);

/**
 * @brief Destroy a coroutine pool
 * 
 * Frees all resources associated with the pool. Safe to call regardless
 * of pool state.
 * 
 * @param pool Pool handle to destroy
 * @return SP_CO_OK on success, SP_CO_ERR_INVALID if pool is NULL
 */
sp_co_result_t sp_co_destroy(sp_co_pool_handle_t pool);

/**
 * @brief Add a coroutine to the pool
 * 
 * Registers a new coroutine function in the pool. The coroutine is created
 * in READY state and can be activated with sp_co_go or sp_co_start.
 * 
 * @param pool Pool handle
 * @param func Coroutine function to execute
 * @param arg Argument passed to the coroutine function
 * @return Coroutine handle on success, NULL if pool/func is NULL or pool is full
 */
sp_co_handle_t sp_co_add(sp_co_pool_handle_t pool, sp_co_func_t func, void* arg);

/**
 * @brief Remove a coroutine from the pool
 * 
 * Frees a coroutine slot for reuse. Coroutine must not be RUNNING.
 * 
 * @param pool Pool handle
 * @param co Coroutine handle to remove
 * @return SP_CO_OK on success, SP_CO_ERR_INVALID if handles are NULL,
 *         SP_CO_ERR_STATE if coroutine is RUNNING or already removed
 */
sp_co_result_t sp_co_remove(sp_co_pool_handle_t pool, sp_co_handle_t co);

/**
 * @brief Get the currently executing coroutine
 * 
 * @param pool Pool handle
 * @return Current coroutine handle, or NULL if no coroutine is running or pool is NULL
 */
sp_co_handle_t sp_co_current(sp_co_pool_handle_t pool);

/**
 * @brief Query coroutine state
 * 
 * @param co Coroutine handle
 * @param out_state Pointer to receive the state value
 * @return SP_CO_OK on success, SP_CO_ERR_INVALID if co or out_state is NULL
 */
sp_co_result_t sp_co_state(sp_co_handle_t co, sp_co_state_t* out_state);

/**
 * @brief Get pool capacity
 * 
 * @param pool Pool handle
 * @return Maximum number of coroutines, or 0 if pool is NULL
 */
size_t sp_co_pool_capacity(sp_co_pool_handle_t pool);

/**
 * @brief Get number of allocated coroutine slots
 * 
 * Returns count of coroutines added to the pool (including DEAD coroutines
 * that haven't been removed yet).
 * 
 * @param pool Pool handle
 * @return Number of allocated slots, or 0 if pool is NULL
 */
size_t sp_co_pool_count(sp_co_pool_handle_t pool);

/**
 * @brief Start the coroutine pool
 * 
 * Activates the initial scheduler coroutine and runs until it completes.
 * The initial coroutine CANNOT yield - it must use sp_co_go to activate
 * other coroutines. Pool cannot be restarted after this function returns.
 * 
 * @param pool Pool handle
 * @param co Initial coroutine to run (must be in READY state)
 * @return SP_CO_OK when scheduler completes, or error code:
 *         SP_CO_ERR_INVALID if handles are NULL,
 *         SP_CO_ERR_STATE if pool already started or coroutine not READY
 */
sp_co_result_t sp_co_start(sp_co_pool_handle_t pool, sp_co_handle_t co);

/**
 * @brief Activate a coroutine
 * 
 * Transfers control to the specified coroutine. When the coroutine yields,
 * control returns to the caller. On first activation of a READY coroutine,
 * assigns one of the pre-built stack frames to the coroutine.
 * 
 * @param pool Pool handle
 * @param co Coroutine to activate (must be READY or SUSPENDED)
 * @return SP_CO_OK when coroutine yields back, or error code:
 *         SP_CO_ERR_INVALID if handles are NULL,
 *         SP_CO_ERR_STATE if called from outside a running coroutine,
 *         SP_CO_ERR_STATE if coroutine is not READY or SUSPENDED,
 *         SP_CO_ERR_STACK_OVERFLOW if the coroutine exceeded its stack budget
 *             (the pool should be considered unusable after this error)
 */
sp_co_result_t sp_co_go(sp_co_pool_handle_t pool, sp_co_handle_t co);

/**
 * @brief Yield control back to caller
 * 
 * Suspends current coroutine and returns control to the coroutine that
 * activated it via sp_co_go. The main coroutine (started with sp_co_start)
 * CANNOT yield.
 * 
 * @param pool Pool handle
 * @return SP_CO_OK when resumed, or error code:
 *         SP_CO_ERR_INVALID if pool is NULL or no coroutine is currently running,
 *         SP_CO_ERR_YIELD_FROM_MAIN if called from main coroutine
 */
sp_co_result_t sp_co_yield(sp_co_pool_handle_t pool);

#endif // SP_COROUTINE_H
