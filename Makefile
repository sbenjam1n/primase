# Makefile for telomere Pd external
#
# Usage:
#   make              — build telomere.pd_linux (or .pd_darwin on macOS)
#   make PD_PATH=/usr/include/pd   — specify Pd headers location
#   make clean        — remove build artifacts
#   make test         — compile-check with stub headers
#   make test_unit    — build and run standalone unit tests

# Detect platform
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  SUFFIX = pd_darwin
  LDFLAGS = -bundle -undefined dynamic_lookup
else
  SUFFIX = pd_linux
  LDFLAGS = -shared -Wl,--export-dynamic
endif

# Pure Data header path (override with: make PD_PATH=/path/to/pd)
PD_PATH ?= pd

CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2 -fPIC -I$(PD_PATH)
TARGET = telomere.$(SUFFIX)

# Source files
CORE_SRC = telomere.c \
           telomere_pattern_api.c \
           telomere_registry.c

TRANSFORM_SRC = transforms/builtins.c \
                transforms/palindrome.c \
                transforms/rotate.c \
                transforms/reverse.c \
                transforms/fast.c \
                transforms/slow.c \
                transforms/euclid.c \
                transforms/jitter.c \
                transforms/skip.c \
                transforms/degrade.c

ALL_SRC = $(CORE_SRC) $(TRANSFORM_SRC)
ALL_OBJ = $(ALL_SRC:.c=.o)

# Default target
all: $(TARGET)

$(TARGET): $(ALL_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Test target: compile with the bundled stub headers
test:
	$(MAKE) PD_PATH=pd all

# Unit test target: standalone runner, no Pd runtime needed
TEST_SRC = tests/test_main.c \
           tests/pd_stub.c \
           telomere_pattern_api.c \
           telomere_registry.c \
           transforms/palindrome.c \
           transforms/rotate.c \
           transforms/reverse.c \
           transforms/fast.c \
           transforms/slow.c \
           transforms/euclid.c \
           transforms/jitter.c \
           transforms/skip.c \
           transforms/degrade.c \
           transforms/builtins.c

test_unit: $(TEST_SRC)
	$(CC) -Wall -O2 -Ipd -I. -o tests/test_runner $(TEST_SRC) -lm
	./tests/test_runner

clean:
	rm -f $(ALL_OBJ) $(TARGET) telomere.pd_linux telomere.pd_darwin tests/test_runner

.PHONY: all clean test test_unit
