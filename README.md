# SPCorLib - Simple Coroutine Library

Asymmetric coroutine library for C using recursive stack frames and setjmp/longjmp.

## Status

✅ **Fully Working** - Multiple yields, resumes, and round-robin scheduling all work correctly!

### What Works
- Creating pools with separate stack budgets per coroutine
- Multiple yield and resume cycles
- Round-robin and cooperative scheduling
- Multiple simultaneous coroutines
- Best-effort stack overflow detection via sentinels
- Portable C99 implementation (no assembly or deprecated APIs)

## Architecture

The library uses a novel **recursive stack-building approach**:

1. Pool created with configurable coroutine count and stack size per coroutine
2. `sp_co_start` recursively calls a function N times (N = capacity)
3. Each recursive call reserves `stack_size` bytes via `alloca`, separating consecutive frames' saved SPs
4. Each frame saves its context with `setjmp`
5. Coroutines are assigned to frames and activated via `longjmp`
6. When coroutines yield, they save context and jump back to caller
7. Frames persist on the stack, keeping contexts valid

This provides per-coroutine stack budgets using only standard C.

## Files

- `sp_coroutine.h` - Public API with full documentation
- `sp_coroutine.c` - Implementation with recursive stack frames
- `Makefile` - Build system
- `test_simple.c` - Simple example with one worker
- `test_multi_worker.c` - Complex example with 3 workers in round-robin
- `test_overflow.c` - Verifies the stack-overflow sentinel triggers
- `LIMITATIONS.md` - Known limitations and constraints

## Building

```bash
make                # Build all examples
make run-simple     # Run simple example
make run-multi      # Run multi-worker example
make run-overflow   # Run overflow-detection test
make clean          # Clean build artifacts
```

## API Summary

```c
// Pool lifecycle
sp_co_pool_handle_t sp_co_create(size_t max_coroutines, size_t stack_size);
sp_co_result_t      sp_co_destroy(sp_co_pool_handle_t pool);

// Coroutine management
sp_co_handle_t      sp_co_add(sp_co_pool_handle_t pool, sp_co_func_t func, void* arg);
sp_co_result_t      sp_co_remove(sp_co_pool_handle_t pool, sp_co_handle_t co);

// Execution control
sp_co_result_t      sp_co_start(sp_co_pool_handle_t pool, sp_co_handle_t co);  // Start main coroutine
sp_co_result_t      sp_co_go(sp_co_pool_handle_t pool, sp_co_handle_t co);     // Activate/resume coroutine
sp_co_result_t      sp_co_yield(sp_co_pool_handle_t pool);                     // Yield to caller

// Queries
sp_co_handle_t      sp_co_current(sp_co_pool_handle_t pool);
sp_co_result_t      sp_co_state(sp_co_handle_t co, sp_co_state_t* out_state);
size_t              sp_co_pool_capacity(sp_co_pool_handle_t pool);
size_t              sp_co_pool_count(sp_co_pool_handle_t pool);
```

## Example Usage

See `test_simple.c` for a basic example and `test_multi_worker.c` for round-robin scheduling with multiple workers.
