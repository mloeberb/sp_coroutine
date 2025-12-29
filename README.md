# SPCorLib - Simple Coroutine Library

Asymmetric coroutine library for C using recursive stack frames and setjmp/longjmp.

## Status

✅ **Fully Working** - Multiple yields, resumes, and round-robin scheduling all work correctly!

### What Works
- Creating pools with separate stack space per coroutine
- Stack sentinel placement for overflow detection  
- Multiple yield and resume cycles
- Round-robin and cooperative scheduling
- Multiple simultaneous coroutines
- Portable C99 implementation (no assembly or deprecated APIs)

## Architecture

The library uses a novel **recursive stack-building approach**:

1. Pool created with configurable coroutine count and stack size per coroutine
2. `sp_co_start` recursively calls a function N times (N = capacity)
3. Each recursive call allocates a local array for that coroutine's stack
4. Each frame saves its context with `setjmp`
5. Coroutines are assigned to frames and activated via `longjmp`
6. When coroutines yield, they save context and jump back to caller
7. Frames persist on the stack, keeping contexts valid

This provides true separate stacks for each coroutine using only standard C!

## Files

- `sp_coroutine.h` - Public API with full documentation
- `sp_coroutine.c` - Implementation with recursive stack frames
- `Makefile` - Build system
- `test_simple.c` - Simple example with one worker
- `test_multi_worker.c` - Complex example with 3 workers in round-robin
- `LIMITATIONS.md` - Known limitations and constraints

## Building

```bash
make                # Build all examples
make run-simple     # Run simple example
make run-multi      # Run multi-worker example
make clean          # Clean build artifacts
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

## Example Usage

See `test_simple.c` for a basic example and `test_multi_worker.c` for round-robin scheduling with multiple workers.
