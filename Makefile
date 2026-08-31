# quilt-c — Makefile (Phase 216)
#
# The minimum viable cell in C. Build with `make`, test with `make test`.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
INCLUDES = -Iinclude
SRC     = src/engine.c
TEST    = tests/test_engine.c
BUILD   = build

.PHONY: all test clean

all: $(BUILD)/libquilt-c.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/libquilt-c.a: $(SRC) include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRC) -o $(BUILD)/engine.o
	ar rcs $@ $(BUILD)/engine.o

$(BUILD)/test_engine: $(TEST) $(SRC) include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST) $(SRC) -o $@

test: $(BUILD)/test_engine
	./$(BUILD)/test_engine

clean:
	rm -rf $(BUILD)
