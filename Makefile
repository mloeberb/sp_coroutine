# Makefile for SPCorLib - Simple Coroutine Library

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LDFLAGS =

# Source files
LIB_SRC = sp_coroutine.c
LIB_OBJ = $(LIB_SRC:.c=.o)

# Examples
EXAMPLES = test_simple test_multi_worker

# Default target
all: $(EXAMPLES)

# Build examples
test_simple: test_simple.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

test_multi_worker: test_multi_worker.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Compile library object files
%.o: %.c sp_coroutine.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean build artifacts
clean:
	rm -f $(LIB_OBJ) $(EXAMPLES)

# Run examples
run-simple: test_simple
	./test_simple

run-multi: test_multi_worker
	./test_multi_worker

.PHONY: all clean run-simple run-multi
