# quilt-c — Makefile (Phase 216)
#
# The minimum viable cell in C. Build with `make`, test with `make test`.

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
INCLUDES = -Iinclude
SRC     = src/engine.c
PROOF   = src/proof.c
ROUTE   = src/route.c
CRDT    = src/crdt.c
TEST    = tests/test_engine.c
TEST_PROOF = tests/test_proof.c
TEST_ROUTE = tests/test_route.c
TEST_CRDT  = tests/test_crdt.c
BUILD   = build

.PHONY: all test test-proof test-route test-crdt test-all clean

all: $(BUILD)/libquilt-c.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/libquilt-c.a: $(SRC) $(PROOF) $(ROUTE) $(CRDT) include/quilt/cell.h include/quilt/proof.h include/quilt/route.h include/quilt/crdt.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRC)  -o $(BUILD)/engine.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(PROOF) -o $(BUILD)/proof.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(ROUTE) -o $(BUILD)/route.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(CRDT)  -o $(BUILD)/crdt.o
	ar rcs $@ $(BUILD)/engine.o $(BUILD)/proof.o $(BUILD)/route.o $(BUILD)/crdt.o

$(BUILD)/test_engine: $(TEST) $(SRC) include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST) $(SRC) -o $@

$(BUILD)/test_proof: $(TEST_PROOF) $(PROOF) $(SRC) include/quilt/proof.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_PROOF) $(PROOF) $(SRC) -o $@

$(BUILD)/test_route: $(TEST_ROUTE) $(ROUTE) $(SRC) include/quilt/route.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_ROUTE) $(ROUTE) $(SRC) -o $@

$(BUILD)/test_crdt: $(TEST_CRDT) $(CRDT) $(SRC) include/quilt/crdt.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_CRDT) $(CRDT) $(SRC) -o $@

test: $(BUILD)/test_engine
	./$(BUILD)/test_engine

test-proof: $(BUILD)/test_proof
	./$(BUILD)/test_proof

test-route: $(BUILD)/test_route
	./$(BUILD)/test_route

test-crdt: $(BUILD)/test_crdt
	./$(BUILD)/test_crdt

test-all: test test-proof test-route test-crdt

clean:
	rm -rf $(BUILD)
