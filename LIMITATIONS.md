# SPCorLib Limitations

## Fundamental Constraint with setjmp/longjmp

This library uses `setjmp/longjmp` for context switching, which has fundamental limitations:

### The Problem

`setjmp` saves the execution context (stack pointer, program counter, registers) at the point it's called. `longjmp` restores that context. However, **the saved context is only valid as long as the stack frame where setjmp was called remains on the stack**.

Once a function returns, its stack frame is unwound, and any saved contexts pointing to that frame become invalid. Jumping to an invalid context causes undefined behavior (usually a segfault).

### What This Means

**Asymmetric coroutines with arbitrary call chains are impossible with pure setjmp/longjmp.**

In the original design, we wanted:
```
Scheduler (main) calls sp_co_go(ping)
  → ping yields back to scheduler
  → scheduler calls sp_co_go(pong) 
  → pong yields back to scheduler
  → repeat
```

This fails because:
1. `sp_co_go(ping)` saves scheduler's context in its stack frame
2. ping yields, jumping back to sp_co_go's saved context
3. `sp_co_go` returns normally, unwinding its frame
4. `sp_co_go(pong)` is called, saving a NEW context in a NEW frame
5. When pong yields, it tries to jump back, but that frame is gone
6. Segfault

### Current Solution

The library now implements a **simplified asymmetric model**:

- The main coroutine executes directly in the trampoline (no setjmp/longjmp for first call)
- Worker coroutines can yield back to a single saved context (the return point in sp_co_start)
- **Workers cannot call sp_co_go to activate other workers**
- The main coroutine can activate workers, but each activation happens in a potentially different stack frame

This means:
- ✅ Main scheduler can call sp_co_go on workers
- ✅ Workers can yield back
- ❌ Workers cannot activate other workers (would save invalid contexts)
- ❌ Complex coroutine call chains are not supported

### Alternative Approaches

To get full asymmetric coroutines with arbitrary call chains, you need:

1. **Platform-specific stack switching**: Use assembly to switch stacks (like `ucontext`, `makecontext`, or manual stack pointer manipulation)
2. **Separate stacks per coroutine**: Allocate actual separate stacks, not just sentinel regions
3. **Fiber libraries**: Use OS-provided fiber APIs (Windows) or third-party libraries

This library deliberately avoids those approaches to remain portable and simple, at the cost of limited coroutine capabilities.

### Recommended Usage Pattern

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

### Why Not Just Fix It?

To support the original design requires:
- Either makecontext/swapcontext (not portable, deprecated)
- Or platform-specific assembly (x86/ARM/etc stack switching code)
- Or a trampoline-based approach where ALL function calls go through a dispatcher (complex, slow)

These solutions violate the design goal of a simple, portable, C99-standard library.
