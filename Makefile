# quilt-c — Makefile (Phase 216)
#
# The minimum viable cell in C. Build with `make`, test with `make test`.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
INCLUDES = -Iinclude
SRC     = src/engine.c
PROOF   = src/proof.c
TEST    = tests/test_engine.c
TEST_PROOF = tests/test_proof.c
BUILD   = build

.PHONY: all test test-proof test-all clean

all: $(BUILD)/libquilt-c.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/libquilt-c.a: $(SRC) $(PROOF) include/quilt/cell.h include/quilt/proof.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRC)  -o $(BUILD)/engine.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(PROOF) -o $(BUILD)/proof.o
	ar rcs $@ $(BUILD)/engine.o $(BUILD)/proof.o

$(BUILD)/test_engine: $(TEST) $(SRC) include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST) $(SRC) -o $@

$(BUILD)/test_proof: $(TEST_PROOF) $(PROOF) $(SRC) include/quilt/proof.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_PROOF) $(PROOF) $(SRC) -o $@

test: $(BUILD)/test_engine
	./$(BUILD)/test_engine

test-proof: $(BUILD)/test_proof
	./$(BUILD)/test_proof

test-all: test test-proof

clean:
	rm -rf $(BUILD)
