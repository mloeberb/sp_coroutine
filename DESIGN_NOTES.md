# SPCorLib - Status and Design Considerations

## Current Status

The implementation has revealed a fundamental limitation: **`setjmp`/`longjmp` alone cannot provide true stackful coroutines with separate stacks**.

### The Problem

- `setjmp`/`longjmp` only save/restore CPU registers (including PC/IP)
- They do NOT save or switch the stack pointer (SP/RSP)
- All coroutines share the same C call stack
- When a function returns, its stack frame is destroyed
- Resuming a coroutine whose activation frame has been unwound causes crashes

### What's Needed for True Stackful Coroutines

To implement true coroutines with separate stacks, we need ONE of:

1. **Platform-specific assembly** to manually switch stack pointers
2. **ucontext.h** API (`makecontext`, `swapcontext`) - POSIX but deprecated  
3. **External libraries** like libco, libtask, or Boost.Context
4. **Language extensions** like GCC's split stacks

###Possible Solutions

#### Option 1: Stackless Coroutines (Generators)
- Simplest with just `setjmp`/`longjmp`
- Coroutines must never return - only yield
- All share the C stack
- Works for simple iteration patterns
- **Limitation**: Can't call yielding functions from nested calls

#### Option 2: Platform-Specific Stack Switching
- Implement `sp_co_switch_stack(void* new_stack, size_t size)` in assembly
- Separate implementations for x86-64, ARM, etc.
- Full coroutine support
- **Complexity**: Requires assembly, platform detection

#### Option 3: Use ucontext (if available)
- Replace `setjmp`/`longjmp` with `getcontext`/`setcontext`/`makecontext`
- Handles stack switching automatically
- **Issue**: Deprecated in POSIX, removed in POSIX.1-2008

## Recommendation

Need to clarify requirements with user:
1. Accept stackless limitation (generators only)?
2. Add platform-specific assembly code?
3. Use ucontext despite deprecation?
4. Use third-party library?
