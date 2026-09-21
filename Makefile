# ---------------------------------------------------------------------
# asmbackend — multi-architecture assembly backend
# ---------------------------------------------------------------------

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS += -Iinclude

# Static library of the compiler backend.
LIB      = libasmbackend.a

# Source files.  Add new backends / modules here.
SRC = \
    src/ir.c \
    src/emitter.c \
    src/backend.c \
    src/assembler.c \
    src/target.c \
    src/target/x86.c \
    src/target/x86_64.c \
    src/target/arm.c \
    src/target/arm64.c \
    src/target/riscv.c

OBJ = $(SRC:.c=.o)
DEP = $(OBJ:.o=.d)

# Default target.
all: $(LIB) examples/hello

# ---------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------

$(LIB): $(OBJ)
	ar rcs $@ $^

# Suffix rule for object files.  -MMD -MP generate .d files that track
# header dependencies, so editing a header rebuilds the .c files that
# include it.
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

# Pull in the auto-generated dependency files if they exist.
-include $(DEP)

# ---------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------

examples/hello: examples/hello.c $(LIB)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -L. -lasmbackend -o $@

# ---------------------------------------------------------------------
# Tests (wire these up when the test dirs exist)
# ---------------------------------------------------------------------

# tests/golden/runner: tests/golden/runner.c $(LIB)
# 	$(CC) $(CFLAGS) $(CPPFLAGS) $< -L. -lasmbackend -o $@

# tests/exec/runner: tests/exec/runner.c $(LIB)
# 	$(CC) $(CFLAGS) $(CPPFLAGS) $< -L. -lasmbackend -o $@

# check: tests/golden/runner tests/exec/runner
# 	./tests/golden/runner
# 	./tests/exec/runner

# ---------------------------------------------------------------------
# Housekeeping
# ---------------------------------------------------------------------

clean:
	rm -f $(OBJ) $(DEP) $(LIB) examples/hello

.PHONY: all clean
