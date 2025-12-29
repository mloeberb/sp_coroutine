# SPCorLib Development Session - 28 December 2025

## Session Overview

Attempted to create a pure C coroutine library using only `setjmp/longjmp` and `alloca()` for memory allocation, avoiding all platform-specific code. **Discovered fundamental limitations that prevent full implementation.**

---

## Project Goals (Original Requirements)

1. ✅ Asymmetric coroutine model (caller/callee relationship)
2. ✅ Yielding returns to the activating coroutine
3. ✅ Configurable stack size per pool
4. ✅ Stack overflow detection with sentinel values (0xDEADC0C0)
5. ✅ Sentinels at both stack boundaries, checked on every yield/resume
6. ✅ Memory allocated with `alloca()` and partitioned among coroutines
7. ✅ Opaque handle types for pools and coroutines
8. ❌ **Full resume capability** - IMPOSSIBLE with setjmp/longjmp alone
9. ✅ Pure C99, no platform-specific code (constraint met, but limits functionality)
10. ❌ No makecontext/ucontext (avoided, but caused limitations)

---

## What Was Built

### File Structure

```
/Users/ml/Documents/C/SPCorLib/
├── sp_coroutine.h              # Public API (complete, stable)
├── sp_coroutine_v3.c           # Current implementation (partially working)
├── sp_coroutine_v2.c           # Previous attempt (pool pointer corruption issue)
├── sp_coroutine_old.c          # Initial naive attempt (backup)
├── sp_coroutine_simple.c       # Early test version (backup)
├── example.c                   # Ping-pong test (crashes on resume)
├── example_debug.c             # Debug version with tracing
├── test_minimal.c              # Simple test (WORKS)
├── test_simple.c               # Progressive test
├── test_simple_debug.c         # Debug version
├── test_no_alloca.c            # Attempted isolation test (incomplete)
├── Makefile                    # Build system
├── README.md                   # Basic documentation
├── DESIGN_NOTES.md             # Technical design discussions
├── STATUS.md                   # Comprehensive status and limitations
├── LIMITATIONS.md              # Detailed constraint explanation
└── SESSION_NOTES.md            # This file
```

### API Implementation Status

| Function | Status | Notes |
|----------|--------|-------|
| `sp_co_create()` | ✅ Working | Creates pool, allocates coroutine array |
| `sp_co_destroy()` | ✅ Working | Frees pool and coroutines |
| `sp_co_add()` | ✅ Working | Adds coroutine to pool |
| `sp_co_remove()` | ✅ Working | Marks coroutine as dead |
| `sp_co_start()` | ✅ Working | Starts main coroutine, uses alloca() |
| `sp_co_go()` | ⚠️ Partial | First activation works, resume crashes |
| `sp_co_yield()` | ⚠️ Partial | First yield works, cannot resume after |
| `sp_co_current()` | ✅ Working | Returns current coroutine handle |
| `sp_co_state()` | ✅ Working | Returns coroutine state |
| `sp_co_pool_capacity()` | ✅ Working | Returns pool capacity |
| `sp_co_pool_count()` | ✅ Working | Returns allocated count |

---

## Technical Implementation Details

### Memory Layout

```
alloca(capacity * stack_size) allocates one large block:

[Coroutine 0 Region: stack_size bytes]
  [Sentinel: 0xDEADC0C0]
  [...usable space...]
  [Sentinel: 0xDEADC0C0]
[Coroutine 1 Region: stack_size bytes]
  [Sentinel: 0xDEADC0C0]
  [...usable space...]
  [Sentinel: 0xDEADC0C0]
...
```

**Note:** These are NOT actual separate stacks. They're sentinel-monitored regions on the C stack used to detect overflow during execution. This was a fundamental misunderstanding that limited the design.

### Data Structures

```c
struct sp_coroutine {
    jmp_buf context;              // Saved execution context (registers, PC, SP)
    int state;                    // READY/RUNNING/SUSPENDED/DEAD
    sp_co_func_t func;            // User function pointer
    void* arg;                    // User argument
    unsigned int* sentinel_low;   // Low boundary sentinel pointer
    unsigned int* sentinel_high;  // High boundary sentinel pointer
    struct sp_coroutine* caller;  // Calling coroutine (asymmetric model)
    int is_main;                  // Flag: is this the main/scheduler coroutine
};

struct sp_co_pool {
    struct sp_coroutine* coroutines;  // Fixed array
    size_t capacity;                  // Max coroutines
    size_t allocated;                 // Current count
    size_t stack_size;                // Per-coroutine stack budget
    void* stack_pool;                 // alloca'd memory base
    size_t pool_size;                 // Total allocation size
    struct sp_coroutine* current;     // Currently executing
    struct sp_coroutine* target;      // Next to activate (unused in v3)
    jmp_buf trampoline;               // Trampoline context (unused in v3)
    jmp_buf return_point;             // Return point in sp_co_start
    int started;                      // Pool has been started
    volatile int should_exit;         // Exit flag (volatile for longjmp safety)
};
```

### Control Flow (v3 Implementation)

```
sp_co_start(pool, main_coroutine)
  └─> setjmp(pool->return_point)  // Save return point
      └─> trampoline(pool, main_co)
          └─> main_co->func()  // Execute directly (no longjmp)
              └─> sp_co_go(pool, worker)
                  ├─> setjmp(worker->context)  // First time: save worker entry
                  ├─> setjmp(main_co->context)  // Save caller's context
                  └─> longjmp(worker->context)  // Jump to worker
                      └─> worker->func()
                          └─> sp_co_yield()
                              ├─> setjmp(worker->context)  // ⚠️ PROBLEM HERE
                              └─> longjmp(main_co->context)  // Return to main
                                  └─> [back in sp_co_go, returns normally]
              └─> sp_co_go(pool, worker)  // ⚠️ Try to resume
                  └─> longjmp(worker->context)  // ⚠️ CRASH - invalid frame
```

---

## The Critical Bug: Why Resume Fails

### Problem Sequence

1. **First activation of worker:**
   ```c
   sp_co_go(pool, worker);
   // setjmp saves worker entry point ✅
   // setjmp saves main_co context ✅
   // longjmp to worker ✅
   ```

2. **Worker executes and yields:**
   ```c
   sp_co_yield(pool);
   // setjmp(worker->context) ⚠️ saves context inside sp_co_yield's frame
   // longjmp(main_co->context) ✅ returns to main
   // sp_co_yield's stack frame is now unwound/gone
   ```

3. **Attempting to resume worker:**
   ```c
   sp_co_go(pool, worker);  // worker->state == SUSPENDED
   // longjmp(worker->context) ⚠️ jumps to sp_co_yield's frame
   // ❌ CRASH: frame no longer exists on stack
   ```

### Root Cause

**setjmp/longjmp can only jump to frames that are currently on the stack.** Once a function returns (normally or via longjmp), its frame is unwound and any saved contexts become invalid.

For coroutines to resume multiple times:
- Each coroutine needs a context saved in a **persistent frame**
- With separate stacks: each coroutine's frame lives on its own stack ✅
- With shared stack + setjmp/longjmp: frames unwind when functions return ❌

### Earlier Bugs Encountered and Fixed

1. **Pool pointer corruption (v2):**
   - Problem: `pool` parameter stored in register, restored to wrong value by longjmp
   - Solution: Made pool pointer `volatile` in trampoline function
   - File: sp_coroutine_v2.c (around line 224)

2. **Trampoline returning normally:**
   - Problem: Trampoline broke from loop and returned, causing stack corruption
   - Solution: Always exit via `longjmp(pool->return_point)`
   - File: sp_coroutine_v3.c (around line 153)

3. **should_exit flag not updating:**
   - Problem: Flag cached in register, longjmp restored old value
   - Solution: Made `should_exit` volatile
   - File: sp_coroutine_v3.c line 57

---

## Test Results

### test_minimal.c - ✅ PASSES
```c
void scheduler(void* arg) {
    printf("Sched: executing\n");
    // Just returns, no yields
}

sp_co_start(pool, sched);  // Works perfectly
```

**Output:**
```
Main: start
[sp_co_start] Entering trampoline
[trampoline] Executing main coroutine directly
Sched: executing
Sched: about to return
[trampoline] Main coroutine completed, exiting
[sp_co_start] Returned from trampoline
Main: done
```

### example.c - ❌ CRASHES
```c
void scheduler(void* arg) {
    for (int i = 0; i < 3; i++) {
        sp_co_go(pool, ping);  // First time: works
        sp_co_go(pool, pong);  // First time: works
        sp_co_go(pool, ping);  // ❌ CRASH: tries to resume
    }
}
```

**Output:**
```
Scheduler: starting ping-pong
Ping! (1)
Pong! (1)
zsh: bus error  ./example
```

**Debug trace shows:**
```
[sp_co_go] Called, co=0x104dda620, state=2  // state=SUSPENDED
[sp_co_go] Caller=0x104dda810 (is_main=1)
[sp_co_go] Saving caller context
[sp_co_go] Jumping to worker
zsh: bus error
```

Crashes immediately after `longjmp(worker->context)` because that context points to an unwound frame.

---

## Why This Cannot Be Fixed With setjmp/longjmp

### Theoretical Analysis

**Requirements for coroutines:**
1. Save execution state (registers, PC, SP)
2. Switch to different execution state
3. Resume from saved state later
4. Repeat multiple times

**What setjmp/longjmp provides:**
1. ✅ Save execution state
2. ✅ Switch to different state
3. ❌ **Cannot resume if frame is unwound**
4. ❌ **Cannot repeat if frames unwind between operations**

**Why separate stacks are needed:**

```
Traditional (shared stack):
[sp_co_start frame]
  [main_co->func frame] ← saved main_co->context points here
    [sp_co_go frame]
      [worker->func frame] ← saved worker->context points here
        [sp_co_yield frame] ← setjmp saves context here
          ← longjmp exits
      ← sp_co_yield frame GONE
    ← sp_co_go returns normally
    ← try to resume: longjmp to GONE frame ❌

With separate stacks:
Stack A (main):        Stack B (worker):
[sp_co_start]         [worker->func]
  [main_co->func]       [sp_co_yield]
    [sp_co_go]            ↑
      |                   | frames persist
      └─→ switch to B     | can longjmp back ✅
```

---

## Solutions and Trade-offs

### Option 1: Platform-Specific Assembly (RECOMMENDED for full features)

**What's needed:**
- Stack allocation (malloc actual stacks, not alloca regions)
- Stack switching code in assembly
- Architecture-specific implementations

**Example (x86-64):**
```c
void switch_to_coroutine(void* new_sp, void* new_bp, void* entry) {
    asm volatile(
        "movq %0, %%rsp\n"    // Switch stack pointer
        "movq %1, %%rbp\n"    // Switch base pointer
        "jmpq *%2\n"          // Jump to entry point
        :: "r"(new_sp), "r"(new_bp), "r"(entry)
    );
}
```

**Pros:**
- Full asymmetric coroutine support ✅
- Multiple yields/resumes ✅
- Efficient ✅
- Real separate stacks ✅

**Cons:**
- ~100-200 lines of assembly per architecture
- Need to support x86-64, ARM64, etc.
- User explicitly rejected this approach

**Estimated effort:** 4-8 hours to implement properly

---

### Option 2: Use ucontext API (DEPRECATED but portable)

**What's needed:**
```c
#include <ucontext.h>

getcontext(&co->uctx);
co->uctx.uc_stack.ss_sp = malloc(stack_size);
co->uctx.uc_stack.ss_size = stack_size;
makecontext(&co->uctx, co->func, 1, arg);
swapcontext(&current->uctx, &co->uctx);
```

**Pros:**
- Portable POSIX API ✅
- Full coroutine support ✅
- No assembly needed ✅

**Cons:**
- Deprecated since POSIX.1-2008
- Not available on all platforms
- User explicitly rejected this approach

**Estimated effort:** 2-3 hours to integrate

---

### Option 3: Keep Limited Implementation (current state)

**Capabilities:**
- Main coroutine executes ✅
- Workers can activate once ✅
- Workers can yield once ✅
- **Cannot resume suspended workers** ❌

**Use case:**
```c
// ONLY this pattern works:
void main_coroutine(void* arg) {
    sp_co_go(pool, worker1);  // worker1 runs and yields
    // cannot call sp_co_go(worker1) again
    sp_co_go(pool, worker2);  // worker2 runs and yields
    sp_co_go(pool, worker3);  // worker3 runs and yields
}
```

**Pros:**
- Pure C99, no assembly ✅
- Meets original portability requirement ✅
- Simple implementation ✅

**Cons:**
- Severely limited functionality ❌
- Cannot implement ping-pong example ❌
- Cannot implement round-robin scheduling ❌
- Workers are essentially one-shot callbacks ❌

**Estimated effort:** Document limitations clearly (1 hour)

---

### Option 4: Use Existing Library

**Recommendations:**
- **libco** - Minimal, portable coroutine library (used by RetroArch)
- **Boost.Context** - C++ library with C API
- **libtask** - Task library by Russ Cox
- **libdill** - Structured concurrency library

**Pros:**
- Full features immediately ✅
- Battle-tested ✅
- Maintained ✅

**Cons:**
- External dependency
- May not match API design
- Learning curve for specific library

**Estimated effort:** 1-2 hours to integrate

---

### Option 5: Symmetric Coroutines (Different Model)

Instead of asymmetric (caller/callee), use symmetric (explicit transfer):

```c
void coroutine_a(void* arg) {
    printf("A\n");
    sp_co_transfer(pool, coroutine_b);  // explicit transfer
    printf("A again\n");
}

void coroutine_b(void* arg) {
    printf("B\n");
    sp_co_transfer(pool, coroutine_a);  // explicit transfer
}
```

**Pros:**
- Simpler to implement with setjmp/longjmp
- Clear control flow
- Can work with current constraints

**Cons:**
- Different programming model
- Requires API redesign
- User asked for asymmetric

**Estimated effort:** 3-4 hours to redesign and implement

---

## Key Technical Learnings

### 1. setjmp/longjmp Behavior with Registers (ARM64)

On ARM64, function parameters are passed in registers (x0-x7). When `setjmp` saves context and `longjmp` restores it, register values are restored to their saved state. This caused the pool pointer corruption:

```c
void trampoline(struct sp_co_pool* pool_param) {
    // pool_param is in register x0
    setjmp(pool->trampoline);  // saves x0 = pool_param
    // ... later, after longjmp from elsewhere ...
    // x0 is restored to OLD value, pool_param is now wrong
}
```

**Fix:** Use `volatile` local variable to force memory storage:
```c
struct sp_co_pool* volatile pool = pool_param;
```

### 2. volatile Is Critical for longjmp Safety

Any variable that might be modified between `setjmp` and `longjmp` must be `volatile`:

```c
volatile int should_exit;  // Must be volatile
```

Without `volatile`, compiler may cache the value in a register, and `longjmp` will restore the old cached value.

### 3. Stack Frame Lifetime

Key insight: **A stack frame only exists while the function is executing.**

```c
void func() {
    jmp_buf buf;
    setjmp(buf);  // saves context pointing to this frame
    // ...
}  // frame is unwound here
// longjmp(buf, 1) is now UNDEFINED BEHAVIOR
```

This is why coroutines need separate stacks - so their frames never unwind.

### 4. alloca() Limitations

`alloca()` allocates on the **current function's stack frame**. It's deallocated when the function returns:

```c
int sp_co_start(...) {
    void* mem = alloca(pool_size);  // allocated on sp_co_start's frame
    // ... use mem ...
    return 0;  // mem is deallocated here
}
```

For coroutines, this means:
- ✅ Can use for sentinel regions (checked during execution)
- ❌ Cannot use as actual separate stacks (need to persist)

### 5. Why Trampoline Approach Failed

The trampoline was meant to provide a persistent frame:

```c
void trampoline(pool) {
    for (;;) {
        setjmp(trampoline_buf);
        // activate coroutines
    }
}
```

But coroutines still save their contexts in **their own frames** (sp_co_yield), which unwind. The trampoline's persistence doesn't help.

---

## Current Code Quality

### Strengths
- ✅ Clean, well-documented API
- ✅ Proper error handling with meaningful codes
- ✅ Opaque handle types
- ✅ Comprehensive sentinel checking
- ✅ Good separation of concerns
- ✅ Extensive debug output for troubleshooting
- ✅ Works perfectly for supported use cases

### Weaknesses
- ❌ Cannot support full asymmetric coroutine model
- ❌ Resume functionality fundamentally broken
- ❌ Memory regions misnamed as "stacks" (they're just sentinel zones)
- ⚠️ Many unused fields in structures (target, trampoline in v3)
- ⚠️ Excessive debug printf statements (should be removed for production)

---

## Build and Test Instructions

### Compile Current Version
```bash
cd /Users/ml/Documents/C/SPCorLib
gcc -std=c99 -O0 -g -o test_minimal test_minimal.c sp_coroutine_v3.c
./test_minimal  # Should work
```

### Compile Example (will crash)
```bash
gcc -std=c99 -O0 -g -o example example.c sp_coroutine_v3.c
./example  # Crashes on resume
```

### Use Makefile
```bash
make clean
make all
./test_minimal  # Works
./example       # Crashes
```

---

## Recommended Next Actions

### If Continuing This Project:

**Decision Point:** Choose your path:

1. **Accept platform-specific code:**
   - Read: `man 2 mmap` for stack allocation
   - Implement: `sp_coroutine_arch.h` with stack switching
   - Support: x86-64 and ARM64 initially
   - Result: Full working coroutine library

2. **Accept limited functionality:**
   - Document: Update README with clear limitations
   - Clean up: Remove debug printfs, unused fields
   - Test: Create examples showing what DOES work
   - Result: One-shot worker callback system

3. **Switch to existing library:**
   - Evaluate: libco (simplest), Boost.Context (most features)
   - Integrate: Wrap in your API if desired
   - Result: Full features, maintained code

### Immediate Tasks (if choosing path 1):

1. **Stack allocation:**
   ```c
   void* sp_co_alloc_stack(size_t size) {
       void* stack = mmap(NULL, size, 
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS,
                          -1, 0);
       return stack;
   }
   ```

2. **Create sp_coroutine_arch.h:**
   ```c
   #if defined(__x86_64__)
       #include "sp_coroutine_x86_64.h"
   #elif defined(__aarch64__)
       #include "sp_coroutine_arm64.h"
   #else
       #error "Unsupported architecture"
   #endif
   ```

3. **Implement context switch:**
   - Save/restore callee-saved registers
   - Switch stack pointer
   - Handle entry point for first activation

---

## References and Resources

### Documentation Consulted
- POSIX setjmp/longjmp specification
- ARM64 calling convention (AAPCS)
- x86-64 System V ABI

### Similar Projects
- **libco:** https://github.com/higan-emu/libco
- **Boost.Context:** https://www.boost.org/doc/libs/release/libs/context/
- **libtask:** http://swtch.com/libtask/

### Relevant Stack Overflow
- "Can setjmp/longjmp be used for coroutines?" - Answer: Only with limitations
- "Why does longjmp crash?" - Stack frame lifetime issues
- "Implementing coroutines in C" - Requires stack switching

---

## Session Timeline

1. **Initial Implementation (sp_coroutine_old.c):**
   - Naive setjmp/longjmp approach
   - Discovered stack frame unwinding on first resume

2. **Trampoline Design (sp_coroutine_v2.c):**
   - Attempted persistent frame approach
   - Hit pool pointer corruption bug
   - Fixed with volatile, but still had completion crash

3. **Simplified Design (sp_coroutine_v3.c):**
   - Main coroutine executes directly
   - Eliminated completion crash
   - But resume still fails

4. **Root Cause Analysis:**
   - Traced through setjmp/longjmp behavior
   - Identified stack frame lifetime issue
   - Concluded setjmp/longjmp insufficient

5. **Documentation:**
   - Created STATUS.md with full analysis
   - Created LIMITATIONS.md explaining constraints
   - Created this SESSION_NOTES.md

---

## Final Assessment

### Can This Be Completed?

**With current constraints (no platform code): NO**

Full asymmetric coroutines require one of:
- Separate stacks + stack switching (assembly)
- OS-provided context switching (makecontext)
- Existing coroutine library

Pure setjmp/longjmp cannot provide this functionality.

### What Was Achieved?

1. Complete, well-designed API ✅
2. Working implementation for limited use cases ✅
3. Comprehensive understanding of why full implementation is impossible ✅
4. Clear documentation of limitations and alternatives ✅

### Was This Worthwhile?

**Yes** - Deep learning experience about:
- setjmp/longjmp mechanics
- Stack frame lifetime
- Register behavior across longjmp
- Why coroutine libraries use platform-specific code
- Trade-offs in API design

### Recommendation

**Do not continue with pure setjmp/longjmp approach.**

Either:
1. Add platform-specific stack switching (best for learning)
2. Use existing library (best for production)
3. Accept severe limitations (best for nothing)

The current code is stable and well-documented. It serves as a good foundation if you choose to add stack switching, or as a learning artifact if you move to a library.

---

## How to Resume This Project

### Quick Start
1. Read this file (you're doing it!)
2. Read STATUS.md for detailed technical analysis
3. Review sp_coroutine_v3.c starting at line 145 (trampoline function)
4. Decide which path forward (see "Recommended Next Actions")

### If You Want Full Coroutines
1. Start with sp_coroutine_arch.h
2. Implement stack switching for your platform
3. Modify sp_co_start to allocate real stacks
4. Modify sp_co_go to switch stacks instead of longjmp

### If You Accept Limitations
1. Clean up debug output in sp_coroutine_v3.c
2. Update README.md with usage limitations
3. Create example showing valid usage pattern
4. Ship as "lightweight callback scheduler"

---

## Contact Points for Questions

When resuming, review these specific code sections:

- **Main coroutine execution:** sp_coroutine_v3.c line 145
- **Worker activation:** sp_coroutine_v3.c line 203
- **Yield implementation:** sp_coroutine_v3.c line 278
- **Pool creation:** sp_coroutine_v3.c line 71
- **Stack initialization:** sp_coroutine_v3.c line 115

Good luck!

---

**Session End: 28 December 2025**  
**Total Time Invested: ~3-4 hours**  
**Lines of Code: ~800 (including tests and documentation)**  
**Conclusion: Fundamentally limited by setjmp/longjmp constraints**
