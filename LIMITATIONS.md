# SPCorLib Limitations

## Implementation Approach

This library uses a **recursive stack-building technique** combined with `setjmp/longjmp` for context switching. During initialization, the library recursively calls a function to build N stack frames (where N is the pool capacity). Each frame allocates its own stack space via a local array, creating separate stacks for each coroutine without requiring platform-specific assembly or deprecated APIs.

## How It Works

The key insight is that recursive function calls create persistent stack frames:

```c
void recursive_stack_builder(pool, depth) {
    char stack_space[stack_size];  // This frame's stack
    
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

### 1. Fixed Stack Size Per Coroutine

Each coroutine gets a fixed stack allocation determined at pool creation (between 16KB and 8MB). Stacks cannot grow dynamically. If a coroutine exceeds its stack space, stack overflow detection will trigger an error.

### 2. Maximum Coroutine Count Limited by Stack Depth

The recursive approach builds N stack frames where N is the pool capacity. Very large N (thousands of coroutines) could exhaust the process's stack space during initialization. Practical limits depend on the platform's stack size limits.

### 3. Pool is Single-Use

Once `sp_co_start()` returns, the recursive stack frames unwind and the `alloca()`-allocated memory is deallocated. The pool cannot be restarted or reused.

### 4. Hierarchical Execution Model

This is asymmetric coroutine model by design:
- ✅ Main scheduler coroutine can call `sp_co_go()` to activate workers
- ✅ Workers can yield back to their caller
- ❌ Workers cannot call `sp_co_go()` to activate other workers directly

This hierarchical model is intentional for asymmetric coroutines. If you need symmetric coroutines (where any coroutine can transfer control to any other), a different library design is required.

### 5. Frame Reuse

When a coroutine completes, its frame becomes available for reuse. However, you cannot have more **simultaneously active** coroutines than the pool capacity. Dead coroutines free their frames automatically.

### 6. No Dynamic Growth

Both stack sizes and pool capacity are fixed at pool creation time and cannot be changed or grown dynamically.

## What Works Perfectly

✅ **Multiple yields and resumes** - Coroutines can yield and be resumed unlimited times  
✅ **Round-robin scheduling** - Multiple coroutines can be scheduled cooperatively  
✅ **Stack overflow detection** - Sentinels detect stack corruption  
✅ **Portable C99** - No assembly, no deprecated APIs, works on all platforms  
✅ **Separate stacks** - Each coroutine has its own stack space via recursive frames  

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
