# Makefile for SPCorLib - Simple Coroutine Library

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LDFLAGS =

# Source files
LIB_SRC = sp_coroutine.c
LIB_OBJ = $(LIB_SRC:.c=.o)

# Examples
EXAMPLES = test_simple test_multi_worker test_overflow test_bad_resumer

# Default target
all: $(EXAMPLES)

# Build examples
test_simple: test_simple.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

test_multi_worker: test_multi_worker.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

test_overflow: test_overflow.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

test_bad_resumer: test_bad_resumer.c $(LIB_OBJ)
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

run-overflow: test_overflow
	./test_overflow

run-bad-resumer: test_bad_resumer
	./test_bad_resumer

# Build and run all tests
test: $(EXAMPLES)
	./test_simple
	./test_multi_worker
	./test_overflow
	./test_bad_resumer

.PHONY: all clean test run-simple run-multi run-overflow run-bad-resumer
