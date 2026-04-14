/**
 * @file sp_coroutine.c
 * @brief Implementation of asymmetric coroutine library using recursive stack frames
 * @author Markus Loeberbauer
 * 
 * This implementation uses a novel recursive stack-building approach combined with
 * setjmp/longjmp for context switching. During initialization, the library recursively
 * calls a function N times (N = pool capacity), with each recursive call allocating
 * a local array for that coroutine's stack space. This provides true separate stacks
 * for each coroutine using only standard C99, without requiring platform-specific
 * assembly or deprecated APIs.
 */

#include "sp_coroutine.h"
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdint.h>
#include <alloca.h>

/* Stack size limits */
#define SP_CO_MIN_STACK_SIZE (16 * 1024)      /* 16 KB */
#define SP_CO_MAX_STACK_SIZE (8 * 1024 * 1024) /* 8 MB */

/* Magic number for stack sentinel validation */
#define SP_CO_STACK_MAGIC 0x4C4F4542

/* Stack alignment */
#define STACK_ALIGN 16

/**
 * @brief Internal coroutine structure
 */
struct sp_coroutine {
    jmp_buf context;              /* Saved execution context */
    int state;                    /* Current state (SP_CO_STATE_*) */
    sp_co_func_t func;            /* User function to execute */
    void* arg;                    /* Argument for user function */
    unsigned int* sentinel_low;   /* Low address sentinel */
    unsigned int* sentinel_high;  /* High address sentinel */
    struct sp_coroutine* caller;  /* Coroutine that activated this one */
    int is_main;                  /* Flag: 1 if this is the main coroutine */
    int started;                  /* Flag: coroutine has begun execution */
    int frame_index;              /* Which stack frame this coroutine owns (-1 if none) */
};

/**
 * @brief Internal pool structure
 */
struct sp_co_pool {
    struct sp_coroutine* coroutines;  /* Fixed array of coroutines */
    size_t capacity;                  /* Maximum number of coroutines */
    size_t allocated;                 /* Number of allocated slots */
    size_t stack_size;                /* Stack budget for each coroutine */
    void* stack_pool;                 /* Base of sentinel memory pool */
    size_t pool_size;                 /* Total size of stack pool */
    struct sp_coroutine* current;     /* Currently executing coroutine */
    jmp_buf main_context;             /* Context to return to from sp_co_start */
    int started;                      /* Flag: pool has been started */
    jmp_buf* frame_contexts;          /* Array of saved contexts for each stack frame */
    struct sp_coroutine** frame_coroutine;  /* Which coroutine owns each frame */
};

/**
 * @brief Check stack sentinels for overflow
 */
static int check_stack_overflow(struct sp_coroutine* co) {
    if (co->sentinel_low && *co->sentinel_low != SP_CO_STACK_MAGIC) {
        return SP_CO_ERR_STACK_OVERFLOW;
    }
    if (co->sentinel_high && *co->sentinel_high != SP_CO_STACK_MAGIC) {
        return SP_CO_ERR_STACK_OVERFLOW;
    }
    return SP_CO_OK;
}

/**
 * @brief Trampoline to execute coroutine function
 */
static void coroutine_exec(struct sp_co_pool* pool, struct sp_coroutine* co) {
    /* Execute user function */
    co->func(co->arg);
    
    /* Function returned - mark as dead and free frame */
    co->state = SP_CO_STATE_DEAD;
    
    /* Free the frame if this coroutine owns one */
    if (co->frame_index >= 0) {
        pool->frame_coroutine[co->frame_index] = NULL;
        co->frame_index = -1;
    }
    
    pool->current = co->caller;
    
    if (co->caller) {
        longjmp(co->caller->context, 1);
    } else {
        /* Returning from main coroutine */
        longjmp(pool->main_context, 1);
    }
}

sp_co_pool_handle_t sp_co_create(size_t max_coroutines, size_t stack_size) {
    /* Validate stack size */
    if (stack_size < SP_CO_MIN_STACK_SIZE || stack_size > SP_CO_MAX_STACK_SIZE) {
        return NULL;
    }
    
    /* Allocate pool structure */
    struct sp_co_pool* pool = (struct sp_co_pool*)malloc(sizeof(struct sp_co_pool));
    if (!pool) {
        return NULL;
    }
    
    /* Allocate coroutine array */
    pool->coroutines = (struct sp_coroutine*)calloc(max_coroutines, sizeof(struct sp_coroutine));
    if (!pool->coroutines) {
        free(pool);
        return NULL;
    }
    
    /* Allocate frame management arrays */
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
    
    /* Initialize frame_index for all coroutines */
    for (size_t i = 0; i < max_coroutines; i++) {
        pool->coroutines[i].frame_index = -1;
    }
    
    /* Align stack size */
    stack_size = (stack_size + STACK_ALIGN - 1) & ~(STACK_ALIGN - 1);
    
    /* Initialize pool fields */
    pool->capacity = max_coroutines;
    pool->allocated = 0;
    pool->stack_size = stack_size;
    pool->stack_pool = NULL;  /* Will be set by alloca in sp_co_start */
    pool->pool_size = stack_size * max_coroutines;
    pool->current = NULL;
    pool->started = 0;
    
    return pool;
}

int sp_co_destroy(sp_co_pool_handle_t pool) {
    if (!pool) {
        return SP_CO_ERR_INVALID;
    }
    
    /* Free frame management arrays */
    free(pool->frame_coroutine);
    free(pool->frame_contexts);
    
    /* Free coroutine array */
    free(pool->coroutines);
    
    /* Free pool structure */
    free(pool);
    
    return SP_CO_OK;
}

sp_co_handle_t sp_co_add(sp_co_pool_handle_t pool, sp_co_func_t func, void* arg) {
    if (!pool || !func) {
        return NULL;
    }
    
    /* Try to find a free slot */
    for (size_t i = 0; i < pool->capacity; i++) {
        if (pool->coroutines[i].func == NULL || pool->coroutines[i].state == SP_CO_STATE_DEAD) {
            struct sp_coroutine* co = &pool->coroutines[i];
            
            /* Check if we're reusing or allocating new */
            int was_allocated = (co->func != NULL);
            
            /* Initialize/reset coroutine */
            memset(co, 0, sizeof(struct sp_coroutine));
            co->func = func;
            co->arg = arg;
            co->state = SP_CO_STATE_READY;
            co->caller = NULL;
            co->is_main = 0;
            co->started = 0;
            co->frame_index = -1;
            
            /* Update allocated count */
            if (!was_allocated) {
                pool->allocated++;
            }
            
            return co;
        }
    }
    
    return NULL;
}

int sp_co_remove(sp_co_pool_handle_t pool, sp_co_handle_t co) {
    if (!pool || !co) {
        return SP_CO_ERR_INVALID;
    }
    
    /* Cannot remove running coroutine */
    if (co->state == SP_CO_STATE_RUNNING) {
        return SP_CO_ERR_STATE;
    }
    
    /* Mark slot as free */
    co->func = NULL;
    co->state = SP_CO_STATE_DEAD;
    
    /* Decrement allocated count */
    if (pool->allocated > 0) {
        pool->allocated--;
    }
    
    return SP_CO_OK;
}

sp_co_handle_t sp_co_current(sp_co_pool_handle_t pool) {
    if (!pool) {
        return NULL;
    }
    return pool->current;
}

int sp_co_state(sp_co_handle_t co, int* out_state) {
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
 * @brief Initialize coroutine sentinel regions from the pool
 */
static void init_coroutine_sentinels(struct sp_co_pool* pool) {
    char* base = (char*)pool->stack_pool;
    
    for (size_t i = 0; i < pool->capacity; i++) {
        struct sp_coroutine* co = &pool->coroutines[i];
        
        /* Calculate this coroutine's sentinel region */
        char* region_base = base + (i * pool->stack_size);
        char* region_top = region_base + pool->stack_size;
        
        /* Place sentinels at boundaries */
        co->sentinel_low = (unsigned int*)region_base;
        co->sentinel_high = (unsigned int*)(region_top - sizeof(unsigned int));
        
        *co->sentinel_low = SP_CO_STACK_MAGIC;
        *co->sentinel_high = SP_CO_STACK_MAGIC;
    }
}

/**
 * @brief Recursively build stack frames for coroutines
 * 
 * This function recursively calls itself to build N stack frames, each with
 * its own stack space reserved via a local array. Each frame saves its context
 * so coroutines can be assigned to frames and longjmp to them later.
 */
static void recursive_stack_builder(struct sp_co_pool* pool, size_t depth, sp_co_handle_t main_co) {
    /* Reserve this frame's stack space */
    volatile char stack_space[pool->stack_size];
    
    /* Touch the array to prevent optimization */
    stack_space[0] = 0;
    stack_space[pool->stack_size - 1] = 0;
    
    /* Save this frame's context */
    if (setjmp(pool->frame_contexts[depth]) == 0) {
        /* First time here - continue building frames */
        if (depth + 1 < pool->capacity) {
            /* Build the next deeper frame */
            recursive_stack_builder(pool, depth + 1, main_co);
        } else {
            /* All frames built - start main coroutine execution */
            pool->current = main_co;
            main_co->state = SP_CO_STATE_RUNNING;
            main_co->started = 1;
            main_co->is_main = 1;
            main_co->frame_index = -1;  /* Main doesn't own a frame */
            
            /* Execute main coroutine */
            coroutine_exec(pool, main_co);
        }
        
        /* When we return here, execution is done */
        return;
    }
    
    /* Longjmp brought us back to this frame - execute assigned coroutine */
    struct sp_coroutine* co = pool->frame_coroutine[depth];
    if (co && co->func) {
        /* First activation - execute the coroutine function */
        if (!co->started) {
            co->started = 1;
            co->state = SP_CO_STATE_RUNNING;
            pool->current = co;
            
            /* Execute the coroutine */
            coroutine_exec(pool, co);
        } else {
            /* Resumed from yield - just continue execution */
            co->state = SP_CO_STATE_RUNNING;
            pool->current = co;
            /* Execution continues naturally after the setjmp in sp_co_yield */
        }
    }
}

int sp_co_start(sp_co_pool_handle_t pool, sp_co_handle_t co) {
    if (!pool || !co) {
        return SP_CO_ERR_INVALID;
    }
    
    /* Check if pool already started */
    if (pool->started) {
        return SP_CO_ERR_STATE;
    }
    
    /* Coroutine must be in READY state */
    if (co->state != SP_CO_STATE_READY) {
        return SP_CO_ERR_STATE;
    }
    
    /* Allocate sentinel pool using alloca */
    void* stack_memory = alloca(pool->pool_size + STACK_ALIGN);
    
    /* Align the pool */
    pool->stack_pool = (void*)(((uintptr_t)stack_memory + STACK_ALIGN - 1) & ~(STACK_ALIGN - 1));
    
    /* Initialize all coroutine sentinel regions */
    init_coroutine_sentinels(pool);
    
    /* Mark pool as started */
    pool->started = 1;
    
    /* Save return context */
    if (setjmp(pool->main_context) == 0) {
        /* Build recursive stack frames and start main coroutine */
        recursive_stack_builder(pool, 0, co);
    }
    
    /* Returned from execution - check for stack overflow */
    int result = SP_CO_OK;
    if (co->frame_index >= 0) {
        result = check_stack_overflow(co);
    }
    
    return result;
}

int sp_co_go(sp_co_pool_handle_t pool, sp_co_handle_t co) {
    if (!pool || !co) {
        return SP_CO_ERR_INVALID;
    }
    
    /* Coroutine must be READY or SUSPENDED */
    if (co->state != SP_CO_STATE_READY && co->state != SP_CO_STATE_SUSPENDED) {
        return SP_CO_ERR_STATE;
    }
    
    /* Check sentinels if suspended */
    if (co->state == SP_CO_STATE_SUSPENDED && co->frame_index >= 0) {
        int overflow = check_stack_overflow(co);
        if (overflow != SP_CO_OK) {
            return overflow;
        }
    }
    
    /* Save current coroutine (caller) */
    struct sp_coroutine* caller = pool->current;
    
    /* Set up caller relationship */
    co->caller = caller;
    
    if (co->state == SP_CO_STATE_READY && co->frame_index < 0) {
        /* First activation - assign a free frame */
        int free_frame = -1;
        for (size_t i = 0; i < pool->capacity; i++) {
            if (pool->frame_coroutine[i] == NULL) {
                free_frame = i;
                break;
            }
        }
        
        if (free_frame < 0) {
            /* No free frames */
            return SP_CO_ERR_POOL_FULL;
        }
        
        /* Assign coroutine to frame */
        co->frame_index = free_frame;
        pool->frame_coroutine[free_frame] = co;
        co->state = SP_CO_STATE_RUNNING;
        
        /* Save caller's context and jump to the frame */
        if (caller && setjmp(caller->context) == 0) {
            longjmp(pool->frame_contexts[free_frame], 1);
        }
        
        /* Returned from coroutine */
        pool->current = caller;
        if (caller) {
            caller->state = SP_CO_STATE_RUNNING;
        }
    } else {
        /* Resume suspended coroutine - jump to its saved context */
        co->state = SP_CO_STATE_RUNNING;
        
        if (caller && setjmp(caller->context) == 0) {
            longjmp(co->context, 1);
        }
        
        /* Returned from coroutine */
        pool->current = caller;
        if (caller) {
            caller->state = SP_CO_STATE_RUNNING;
        }
    }
    
    /* Check sentinels after return */
    if (co->frame_index >= 0) {
        int overflow = check_stack_overflow(co);
        if (overflow != SP_CO_OK) {
            return overflow;
        }
    }
    
    return SP_CO_OK;
}

int sp_co_yield(sp_co_pool_handle_t pool) {
    if (!pool) {
        return SP_CO_ERR_INVALID;
    }
    
    struct sp_coroutine* current = pool->current;
    
    if (!current) {
        return SP_CO_ERR_INVALID;
    }
    
    /* Main coroutine cannot yield */
    if (current->is_main) {
        return SP_CO_ERR_YIELD_FROM_MAIN;
    }
    
    /* Must have a caller */
    if (!current->caller) {
        return SP_CO_ERR_INVALID;
    }
    
    /* Check sentinels */
    int overflow = check_stack_overflow(current);
    if (overflow != SP_CO_OK) {
        return overflow;
    }
    
    /* Suspend and switch to caller */
    current->state = SP_CO_STATE_SUSPENDED;
    pool->current = current->caller;
    
    if (setjmp(current->context) == 0) {
        longjmp(current->caller->context, 1);
    }
    
    /* Resumed - restore pool->current */
    pool->current = current;
    current->state = SP_CO_STATE_RUNNING;
    
    return SP_CO_OK;
}
