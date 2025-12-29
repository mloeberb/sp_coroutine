# SPCorLib - Simple Coroutine Library

Asymmetric coroutine library for C using setjmp/longjmp with stack partitioning via alloca().

## Current Status

⚠️ **Known Issue**: The implementation has a fundamental limitation with setjmp/longjmp when coroutines are resumed multiple times. This is due to stack frame lifecycle - when `sp_co_go` returns, its stack frame is unwound, but the coroutine's saved context still references that frame.

### What Works
- Creating pools with alloca()-allocated memory partitioned among coroutines
- Stack sentinel placement for overflow detection  
- First activation of coroutines
- Single yield-resume cycle

### What Doesn't Work
- Multiple yield-resume cycles (segfault on second resume)
- This is a fundamental limitation of setjmp/longjmp with shared stacks

## The Problem

With setjmp/longjmp:
1. All coroutines share the C call stack
2. `setjmp` saves register state including stack pointer
3. When `sp_co_go` returns, its stack frame is deallocated
4. Attempting to `longjmp` back to that context causes undefined behavior

## Solutions

To fix this, one of the following is needed:

1. **Use ucontext API** - Provides proper coroutine support but deprecated in POSIX
2. **Platform-specific assembly** - Manually implement stack switching  
3. **Redesign API** - Make `sp_co_go` not return until coroutine completes
4. **Accept limitation** - Document that coroutines can only yield once

## Architecture

The current design:
- Pool created with configurable coroutine count and stack size per coroutine
- `sp_co_start` uses `alloca(capacity * stack_size)` to allocate monitoring region
- Region partitioned with sentinels at boundaries (0xDEADC0C0)
- Sentinels checked on every yield/resume to detect stack overflow
- setjmp/longjmp handles context switching (registers only, not actual stack switching)

## Files

- `sp_coroutine.h` - Public API with full documentation
- `sp_coroutine.c` - Implementation with alloca() partitioning and sentinels
- `Makefile` - Build system
- `example.c` - Ping-pong demonstration
- `DESIGN_NOTES.md` - Technical design discussions

## Building

```bash
make            # Build library and example
make run        # Run example
make clean      # Clean build artifacts
```

## API Summary

```c
// Pool lifecycle
sp_co_pool_handle_t sp_co_create(size_t max_coroutines, size_t stack_size);
int sp_co_destroy(sp_co_pool_handle_t pool);

// Coroutine management  
sp_co_handle_t sp_co_add(sp_co_pool_handle_t pool, sp_co_func_t func, void* arg);
int sp_co_remove(sp_co_pool_handle_t pool, sp_co_handle_t co);

// Execution control
int sp_co_start(sp_co_pool_handle_t pool, sp_co_handle_t co);  // Start main coroutine
int sp_co_go(sp_co_pool_handle_t pool, sp_co_handle_t co);     // Activate/resume coroutine
int sp_co_yield(sp_co_pool_handle_t pool);                     // Yield to caller

// Queries
sp_co_handle_t sp_co_current(sp_co_pool_handle_t pool);
int sp_co_state(sp_co_handle_t co, int* out_state);
size_t sp_co_pool_capacity(sp_co_pool_handle_t pool);
size_t sp_co_pool_count(sp_co_pool_handle_t pool);
```

## Next Steps

To complete the implementation, choose one of:

1. Replace setjmp/longjmp with ucontext (`makecontext`/`swapcontext`)
2. Add platform-specific stack switching in assembly
3. Restructure to keep `sp_co_go` stack frames alive

The current code provides a solid foundation with all the infrastructure (pool management, sentinels, API design) but needs one of the above approaches for full coroutine support.
