# SPCorLib - Current Status

## Summary

The coroutine library has been implemented and partially works, but **fundamental limitations of setjmp/longjmp prevent full asymmetric coroutine support**.

## What Works

✅ Pool creation and destruction  
✅ Adding coroutines to the pool  
✅ Starting a main scheduler coroutine  
✅ Main coroutine activating workers (first activation)  
✅ Workers yielding back to main (first yield)  
✅ Memory allocation with alloca()  
✅ Stack sentinel placement and checking  
✅ Main coroutine completing normally  

## What Doesn't Work

❌ Resuming suspended coroutines  
❌ Workers that yield multiple times  
❌ Complex ping-pong or round-robin scheduling  

## The Fundamental Problem

`setjmp/longjmp` can only jump to contexts that are **still on the stack**. Once a function returns, its stack frame is unwound and any saved contexts become invalid.

### Example Failure

```c
void scheduler() {
    sp_co_go(pool, ping);  // ping yields
    sp_co_go(pool, ping);  // CRASH - trying to resume ping
}
```

**Why it crashes:**

1. First `sp_co_go(ping)` saves scheduler's context via setjmp
2. ping executes and calls `sp_co_yield()`, which:
   - Saves ping's context via setjmp (inside sp_co_yield's frame)
   - Jumps back to scheduler
3. Second `sp_co_go(ping)` tries to resume ping by jumping to ping's saved context
4. **CRASH**: ping's context points to sp_co_yield's stack frame, which was unwound in step 2

### The Core Issue

Every `setjmp` saves a context pointing to the current stack frame. When that function returns (normally or via longjmp), the frame is gone. Any future longjmp to that context is undefined behavior.

For coroutines to work:
- Each coroutine needs a context that remains valid across multiple activations
- With separate stacks: Each coroutine has its own stack, contexts remain valid
- With setjmp/longjmp only: **No way to keep frames valid after they unwind**

## What's Required for Full Support

To support the original ping-pong example and full asymmetric coroutines, you need **ONE** of:

### Option 1: Platform-Specific Stack Switching (RECOMMENDED)

```c
// x86-64 assembly example
asm volatile(
    "movq %0, %%rsp\n"      // Switch to coroutine's stack
    "movq %1, %%rbp\n"      // Set frame pointer
    "jmpq *%2\n"            // Jump to coroutine
    :: "r"(new_stack), "r"(new_frame), "r"(entry_point)
);
```

**Pros:**
- Full coroutine support
- Efficient
- Well-understood

**Cons:**
- Requires assembly for each architecture (x86, ARM, etc.)
- Not portable without conditional compilation
- User explicitly rejected this approach

### Option 2: ucontext API (DEPRECATED)

```c
getcontext(&co->uctx);
co->uctx.uc_stack.ss_sp = stack;
co->uctx.uc_stack.ss_size = stack_size;
co->uctx.uc_link = &caller_uctx;
makecontext(&co->uctx, co_func, 1, arg);
swapcontext(&caller_uctx, &co->uctx);
```

**Pros:**
- Portable API
- Full coroutine support

**Cons:**
- Deprecated in POSIX
- Not available on all platforms
- User explicitly rejected this approach

### Option 3: Fiber/Coroutine Libraries

Use existing libraries like:
- **Boost.Context** (C++)
- **libtask** (C)
- **libco** (C)
- **libdill** (C)

**Pros:**
- Full-featured
- Well-tested
- Handle platform differences

**Cons:**
- External dependency
- May use rejected approaches internally

### Option 4: Very Limited setjmp/longjmp Model

Keep current implementation with documented limitations:

```c
// SUPPORTED: One-shot workers
void worker(void* arg) {
    printf("Working...\n");
    sp_co_yield(pool);  // Yield once
    // Cannot be resumed
}

void scheduler(void* arg) {
    sp_co_go(pool, w1);  // w1 executes and yields
    // Cannot call sp_co_go(w1) again!
    sp_co_go(pool, w2);  // w2 executes and yields
}
```

**Pros:**
- Pure C, portable
- No platform-specific code
- Meets original constraint

**Cons:**
- **Severely limited functionality**
- Cannot resume coroutines
- Workers can only yield once
- Not useful for most real applications

## Recommendation

**The user's requirements are fundamentally incompatible.**

You cannot have:
- Full asymmetric coroutines (with resume capability)  
- AND pure setjmp/longjmp (no platform-specific code)  
- AND alloca()-based allocation

You must choose:

1. **Accept platform-specific code** → Get full coroutines
2. **Stay with setjmp/longjmp only** → Get very limited one-shot yields
3. **Use existing library** → Get full features without writing custom code

## Current Code Status

The v3 implementation (`sp_coroutine_v3.c`) is the cleanest design possible with setjmp/longjmp:

- Main coroutine executes directly in trampoline
- Workers can be activated once
- Workers can yield once
- No resume support

The test_minimal.c works perfectly.  
The example.c crashes on the second round of ping-pong.

## Next Steps

**Decision needed from user:**

1. Accept platform-specific stack switching code?
2. Accept very limited coroutine model (no resume)?
3. Switch to an existing library?
4. Abandon the project?

Without changing requirements, full coroutine support is impossible.
