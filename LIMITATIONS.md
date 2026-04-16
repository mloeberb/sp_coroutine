# SPCorLib Limitations

## Implementation Approach

This library uses a **recursive stack-building technique** combined with `setjmp/longjmp` for context switching. During initialization, the library recursively calls a function to build N stack frames (where N is the pool capacity). Each frame reserves `stack_size` bytes via `alloca`, giving each coroutine its own per-frame stack budget without requiring platform-specific assembly or deprecated APIs.

## How It Works

The key insight is that recursive function calls create persistent stack frames:

```c
void recursive_stack_builder(pool, depth) {
    char* stack_space = alloca(stack_size);  // This frame's stack

    if (setjmp(frame_contexts[depth]) == 0) {
        // First time: build deeper frames
        if (depth < capacity - 1)
            recursive_stack_builder(pool, depth + 1);
        else
            execute_main_coroutine();
    } else {
        // Longjmp brought us back: execute assigned coroutine
        execute_coroutine_at_depth(depth);
    }
}
```

When a coroutine is activated, it's assigned to a free frame and we longjmp to that frame. The coroutine executes in that frame's stack space. When it yields, the context is saved and we longjmp back to the caller. The frame remains on the stack, so the saved context stays valid.

## Actual Limitations

### 1. Fixed Stack Size Per Coroutine, Best-Effort Overflow Detection

Each coroutine gets a fixed stack allocation determined at pool creation (between 16KB and 8MB). Stacks cannot grow dynamically.

All coroutine stacks share one contiguous process-stack region; each frame's `alloca` reserves `stack_size` bytes between consecutive frames' saved SPs. A magic sentinel is placed at the boundary between consecutive frames' regions; if a coroutine exceeds its `stack_size` budget by ~`stack_size` bytes, it corrupts the sentinel and `sp_co_go` returns `SP_CO_ERR_STACK_OVERFLOW` when the coroutine next yields or completes.

The detection threshold is approximate (within a small prologue overhead) and only catches overflow that *densely* writes across the SP range — normal function calls do, but code that allocates a large local array and writes to it sparsely can skip the sentinel. Once `SP_CO_ERR_STACK_OVERFLOW` is returned, the pool must be considered unusable and should be destroyed.

### 2. Maximum Coroutine Count Limited by Stack Depth

The recursive approach builds N stack frames where N is the pool capacity. Very large N (thousands of coroutines) could exhaust the process's stack space during initialization. Practical limits depend on the platform's stack size limits.

### 3. Pool is Single-Use

Once `sp_co_start()` returns, the recursive stack frames unwind and the `alloca()`-allocated memory is deallocated. The pool cannot be restarted or reused.

### 4. Hierarchical Execution Model

This is an asymmetric coroutine model by design. Each `sp_co_go()` call records the activating coroutine as the callee's caller, so the activation graph forms a tree rooted at the main coroutine:

- ✅ Main scheduler coroutine can call `sp_co_go()` to activate workers
- ✅ Workers can yield back to their caller
- ✅ Workers can call `sp_co_go()` to activate other coroutines, extending the tree (A → B → C)
- ❌ Main coroutine cannot yield (it has no caller)
- ❌ Re-activating a suspended coroutine from a different caller overwrites its caller pointer, breaking the original chain

If you need symmetric coroutines (where any coroutine can transfer control to any other without tree constraints), a different library design is required.

### 5. Frame Reuse

When a coroutine completes, its frame becomes available for reuse. However, you cannot have more **simultaneously active** coroutines than the pool capacity. Dead coroutines free their frames automatically.

### 6. No Dynamic Growth

Both stack sizes and pool capacity are fixed at pool creation time and cannot be changed or grown dynamically.

### 7. Single-Threaded

The library is not thread-safe. A pool, and all coroutines in it, must be created and used from a single thread. Multiple threads may each run their own independent pool.

### 8. Windows: Avoid SEH Across Coroutine Boundaries

On Windows, MSVC's `setjmp` participates in Structured Exception Handling (SEH) unwinding. `longjmp` unwinds SEH frames between the `setjmp` and `longjmp` points. This library `longjmp`s across many recursive frames, so wrapping coroutine code in `__try`/`__except` (or using libraries that register SEH handlers within coroutine stacks) can lead to unexpected unwinding behaviour. Plain C code in coroutines works as expected.

## What Works

✅ **Multiple yields and resumes** - Coroutines can yield and be resumed unlimited times
✅ **Round-robin scheduling** - Multiple coroutines can be scheduled cooperatively
✅ **Portable C99** - No assembly, no deprecated APIs, builds on Windows (cl), Linux, and macOS
✅ **Per-coroutine stack budgets** - Each coroutine gets its own `stack_size`-byte region via recursive frames
✅ **Best-effort overflow detection** - Sentinels detect most stack overflows (see limitation #1)

## Recommended Usage Pattern

```c
void worker(void* arg) {
    for (int i = 0; i < 10; i++) {
        // do work
        sp_co_yield(pool);  // yield back to main
    }
}

void main_scheduler(void* arg) {
    // Create workers
    sp_co_handle_t w1 = sp_co_add(pool, worker, data1);
    sp_co_handle_t w2 = sp_co_add(pool, worker, data2);
    
    // Run workers in round-robin
    for (int round = 0; round < 10; round++) {
        sp_co_go(pool, w1);  // w1 executes, yields back
        sp_co_go(pool, w2);  // w2 executes, yields back
    }
}

// Start the scheduler
sp_co_start(pool, scheduler_co);
```

This pattern works reliably with unlimited yields and resumes per coroutine.
