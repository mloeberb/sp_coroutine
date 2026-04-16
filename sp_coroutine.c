/**
 * @file sp_coroutine.c
 * @brief Implementation of asymmetric coroutine library using recursive stack frames
 * @author Markus Loeberbauer <markus.loeberbauer@signum.plus>
 *
 * This implementation uses a recursive stack-building approach combined with
 * setjmp/longjmp for context switching. During initialization, the library
 * recursively calls a function N times (N = pool capacity), with each recursive
 * call reserving stack space via alloca. Each frame saves its context; a
 * coroutine is longjmp'd into a frame to run on that frame's stack.
 *
 * Note: all coroutines share one contiguous process stack region. Each frame's
 * alloca reserves stack_size bytes that separate consecutive frames' saved SPs.
 * A coroutine running in frame K grows its stack down past its own SP into
 * frame K+1's alloca region (which is dead space for frame K+1's own coroutine,
 * since that coroutine runs at an even lower SP). We place a magic sentinel
 * at the bottom of frame K+1's alloca region; if coroutine K overflows its
 * stack_size budget by ~stack_size bytes, it corrupts the sentinel, and the
 * overflow is detected on return. Once detected, the pool is unreliable and
 * should be destroyed.
 */

#include "sp_coroutine.h"
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdbool.h>
#ifdef _MSC_VER
#include <malloc.h>
#define alloca _alloca
#else
#include <alloca.h>
#endif

#define SP_CO_MIN_STACK_SIZE ((size_t)16 * 1024)       // 16 KB
#define SP_CO_MAX_STACK_SIZE ((size_t)8 * 1024 * 1024) // 8 MB
#define SP_CO_STACK_ALIGN    ((size_t)16)
#define SP_CO_STACK_MAGIC    ((unsigned int)0x4C4F4542u)

/**
 * @brief Internal coroutine structure
 */
struct sp_coroutine {
    jmp_buf context;              // Saved execution context
    sp_co_state_t state;          // Current state
    sp_co_func_t func;            // User function to execute
    void* arg;                    // Argument for user function
    struct sp_coroutine* caller;  // Coroutine that activated this one
    bool is_main;                 // True if this is the main coroutine
    int frame_index;              // Which stack frame this coroutine owns (-1 if none)
};

/**
 * @brief Internal pool structure
 */
struct sp_co_pool {
    struct sp_coroutine* coroutines;           // Fixed array of coroutines
    size_t capacity;                           // Maximum number of coroutines
    size_t allocated;                          // Number of allocated slots
    size_t stack_size;                         // Stack budget for each coroutine
    struct sp_coroutine* current;              // Currently executing coroutine
    jmp_buf main_context;                      // Context to return to from sp_co_start
    bool started;                              // True if pool has been started
    jmp_buf* frame_contexts;                   // Saved context for each stack frame
    struct sp_coroutine** frame_coroutine;     // Which coroutine owns each frame
    volatile unsigned int** frame_sentinel;    // Overflow sentinel per frame (NULL for the deepest frame)
};

static void reset_frame_sentinel(struct sp_co_pool* pool, int frame_index) {
    if (frame_index >= 0 && pool->frame_sentinel[frame_index]) {
        *pool->frame_sentinel[frame_index] = SP_CO_STACK_MAGIC;
    }
}

static sp_co_result_t check_frame_sentinel(struct sp_co_pool* pool, int frame_index) {
    if (frame_index < 0 || !pool->frame_sentinel[frame_index]) {
        return SP_CO_OK;
    }
    if (*pool->frame_sentinel[frame_index] != SP_CO_STACK_MAGIC) {
        return SP_CO_ERR_STACK_OVERFLOW;
    }
    return SP_CO_OK;
}

/**
 * @brief Trampoline to execute coroutine function
 */
static void coroutine_exec(struct sp_co_pool* pool, struct sp_coroutine* co) {
    co->func(co->arg);

    // Function returned - mark as dead and free frame
    co->state = SP_CO_STATE_DEAD;
    if (co->frame_index >= 0) {
        pool->frame_coroutine[co->frame_index] = NULL;
        co->frame_index = -1;
    }

    pool->current = co->caller;

    if (co->caller) {
        longjmp(co->caller->context, 1);
    } else {
        // Returning from main coroutine
        longjmp(pool->main_context, 1);
    }
}

sp_co_pool_handle_t sp_co_create(size_t max_coroutines, size_t stack_size) {
    if (stack_size < SP_CO_MIN_STACK_SIZE || stack_size > SP_CO_MAX_STACK_SIZE) {
        return NULL;
    }

    struct sp_co_pool* pool = (struct sp_co_pool*)malloc(sizeof(struct sp_co_pool));
    if (!pool) {
        return NULL;
    }

    pool->coroutines = (struct sp_coroutine*)calloc(max_coroutines, sizeof(struct sp_coroutine));
    if (!pool->coroutines) {
        free(pool);
        return NULL;
    }

    pool->frame_contexts = (jmp_buf*)calloc(max_coroutines, sizeof(jmp_buf));
    if (!pool->frame_contexts) {
        free(pool->coroutines);
        free(pool);
        return NULL;
    }

    pool->frame_coroutine = (struct sp_coroutine**)calloc(max_coroutines, sizeof(struct sp_coroutine*));
    if (!pool->frame_coroutine) {
        free(pool->frame_contexts);
        free(pool->coroutines);
        free(pool);
        return NULL;
    }

    pool->frame_sentinel = (volatile unsigned int**)calloc(max_coroutines, sizeof(volatile unsigned int*));
    if (!pool->frame_sentinel) {
        free(pool->frame_coroutine);
        free(pool->frame_contexts);
        free(pool->coroutines);
        free(pool);
        return NULL;
    }

    // Align stack size up to SP_CO_STACK_ALIGN
    stack_size = (stack_size + SP_CO_STACK_ALIGN - 1) & ~(SP_CO_STACK_ALIGN - 1);

    pool->capacity = max_coroutines;
    pool->allocated = 0;
    pool->stack_size = stack_size;
    pool->current = NULL;
    pool->started = false;

    return pool;
}

sp_co_result_t sp_co_destroy(sp_co_pool_handle_t pool) {
    if (!pool) {
        return SP_CO_ERR_INVALID;
    }

    free(pool->frame_sentinel);
    free(pool->frame_coroutine);
    free(pool->frame_contexts);
    free(pool->coroutines);
    free(pool);

    return SP_CO_OK;
}

sp_co_handle_t sp_co_add(sp_co_pool_handle_t pool, sp_co_func_t func, void* arg) {
    if (!pool || !func) {
        return NULL;
    }

    for (size_t i = 0; i < pool->capacity; i++) {
        if (pool->coroutines[i].func == NULL || pool->coroutines[i].state == SP_CO_STATE_DEAD) {
            struct sp_coroutine* co = &pool->coroutines[i];

            // A freshly removed or never-used slot has func == NULL and is not
            // counted in pool->allocated; a naturally-dead slot still has its
            // func set and is still counted.
            bool slot_counted = (co->func != NULL);

            memset(co, 0, sizeof(struct sp_coroutine));
            co->func = func;
            co->arg = arg;
            co->state = SP_CO_STATE_READY;
            co->frame_index = -1;

            if (!slot_counted) {
                pool->allocated++;
            }

            return co;
        }
    }

    return NULL;
}

sp_co_result_t sp_co_remove(sp_co_pool_handle_t pool, sp_co_handle_t co) {
    if (!pool || !co) {
        return SP_CO_ERR_INVALID;
    }

    if (co->state == SP_CO_STATE_RUNNING) {
        return SP_CO_ERR_STATE;
    }

    // Slot already removed (func cleared); reject double-remove
    if (co->func == NULL) {
        return SP_CO_ERR_STATE;
    }

    if (co->frame_index >= 0) {
        pool->frame_coroutine[co->frame_index] = NULL;
        co->frame_index = -1;
    }

    co->func = NULL;
    co->state = SP_CO_STATE_DEAD;
    pool->allocated--;

    return SP_CO_OK;
}

sp_co_handle_t sp_co_current(sp_co_pool_handle_t pool) {
    if (!pool) {
        return NULL;
    }
    return pool->current;
}

sp_co_result_t sp_co_state(sp_co_handle_t co, sp_co_state_t* out_state) {
    if (!co || !out_state) {
        return SP_CO_ERR_INVALID;
    }
    *out_state = co->state;
    return SP_CO_OK;
}

size_t sp_co_pool_capacity(sp_co_pool_handle_t pool) {
    if (!pool) {
        return 0;
    }
    return pool->capacity;
}

size_t sp_co_pool_count(sp_co_pool_handle_t pool) {
    if (!pool) {
        return 0;
    }
    return pool->allocated;
}

/**
 * @brief Recursively build stack frames for coroutines
 *
 * Each recursion level reserves stack_size bytes via alloca and saves its
 * context. Coroutines are longjmp'd to a frame's context on first activation.
 * After first activation, a coroutine's own context (saved at yield) is used
 * directly; this function is re-entered only on first activations.
 */
static void recursive_stack_builder(struct sp_co_pool* pool, size_t depth, sp_co_handle_t main_co) {
    volatile char* stack_space = (volatile char*)alloca(pool->stack_size);

    // Touch both ends so the compiler cannot elide the allocation and so
    // Windows page probing commits the full region.
    stack_space[0] = 0;
    stack_space[pool->stack_size - 1] = 0;

    // The previous frame's coroutine grows its stack down into this frame's
    // alloca region. Place a sentinel at the bottom of that region; it will
    // be corrupted if the previous coroutine overflows its stack_size budget.
    if (depth > 0) {
        volatile unsigned int* sentinel = (volatile unsigned int*)stack_space;
        *sentinel = SP_CO_STACK_MAGIC;
        pool->frame_sentinel[depth - 1] = sentinel;
    }

    if (setjmp(pool->frame_contexts[depth]) == 0) {
        if (depth + 1 < pool->capacity) {
            recursive_stack_builder(pool, depth + 1, main_co);
        } else {
            // All frames built - start main coroutine execution
            pool->current = main_co;
            main_co->state = SP_CO_STATE_RUNNING;
            main_co->is_main = true;
            main_co->frame_index = -1;  // Main doesn't own a frame
            coroutine_exec(pool, main_co);
        }
        return;
    }

    // Longjmp'd here to run a newly-assigned coroutine in this frame
    struct sp_coroutine* co = pool->frame_coroutine[depth];
    co->state = SP_CO_STATE_RUNNING;
    pool->current = co;
    coroutine_exec(pool, co);
}

sp_co_result_t sp_co_start(sp_co_pool_handle_t pool, sp_co_handle_t co) {
    if (!pool || !co) {
        return SP_CO_ERR_INVALID;
    }

    if (pool->started) {
        return SP_CO_ERR_STATE;
    }

    if (co->state != SP_CO_STATE_READY) {
        return SP_CO_ERR_STATE;
    }

    pool->started = true;

    if (setjmp(pool->main_context) == 0) {
        recursive_stack_builder(pool, 0, co);
    }

    return SP_CO_OK;
}

sp_co_result_t sp_co_go(sp_co_pool_handle_t pool, sp_co_handle_t co) {
    if (!pool || !co) {
        return SP_CO_ERR_INVALID;
    }

    // sp_co_go must be called from within a running coroutine
    struct sp_coroutine* caller = pool->current;
    if (!caller) {
        return SP_CO_ERR_STATE;
    }

    if (co->state != SP_CO_STATE_READY && co->state != SP_CO_STATE_SUSPENDED) {
        return SP_CO_ERR_STATE;
    }

    co->caller = caller;

    int frame_to_check;
    if (co->state == SP_CO_STATE_READY && co->frame_index < 0) {
        // First activation - assign a free frame.
        // A free frame is guaranteed: main holds no frame, so at most
        // capacity-1 non-main coroutines can hold frames simultaneously.
        size_t free_frame = 0;
        while (free_frame < pool->capacity && pool->frame_coroutine[free_frame] != NULL) {
            free_frame++;
        }
        if (free_frame == pool->capacity) {
            return SP_CO_ERR_STATE;  // Invariant broken
        }

        co->frame_index = (int)free_frame;
        pool->frame_coroutine[free_frame] = co;
        co->state = SP_CO_STATE_RUNNING;
        frame_to_check = (int)free_frame;

        reset_frame_sentinel(pool, frame_to_check);
        if (setjmp(caller->context) == 0) {
            longjmp(pool->frame_contexts[free_frame], 1);
        }
    } else {
        // Resume suspended coroutine - jump to its saved context
        co->state = SP_CO_STATE_RUNNING;
        frame_to_check = co->frame_index;

        reset_frame_sentinel(pool, frame_to_check);
        if (setjmp(caller->context) == 0) {
            longjmp(co->context, 1);
        }
    }

    // Returned from coroutine - pool->current and caller->state were
    // already restored by sp_co_yield or coroutine_exec. Check that the
    // coroutine didn't overflow its stack budget.
    return check_frame_sentinel(pool, frame_to_check);
}

sp_co_result_t sp_co_yield(sp_co_pool_handle_t pool) {
    if (!pool) {
        return SP_CO_ERR_INVALID;
    }

    struct sp_coroutine* current = pool->current;
    if (!current) {
        return SP_CO_ERR_INVALID;
    }

    if (current->is_main) {
        return SP_CO_ERR_YIELD_FROM_MAIN;
    }

    if (!current->caller) {
        return SP_CO_ERR_INVALID;
    }

    current->state = SP_CO_STATE_SUSPENDED;
    pool->current = current->caller;

    if (setjmp(current->context) == 0) {
        longjmp(current->caller->context, 1);
    }

    // Resumed
    pool->current = current;
    current->state = SP_CO_STATE_RUNNING;

    return SP_CO_OK;
}
