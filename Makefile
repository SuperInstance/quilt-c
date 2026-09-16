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
WORLD   = src/world.c
TIME    = src/time.c
QUF     = src/quf.c
TEST    = tests/test_engine.c
TEST_PROOF = tests/test_proof.c
TEST_ROUTE = tests/test_route.c
TEST_CRDT  = tests/test_crdt.c
TEST_WORLD = tests/test_world.c
TEST_TIME  = tests/test_time.c
TEST_QUF   = tests/test_quf.c
BUILD   = build

.PHONY: all test test-proof test-route test-crdt test-world test-time test-quf test-all clean

all: $(BUILD)/libquilt-c.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/libquilt-c.a: $(SRC) $(PROOF) $(ROUTE) $(CRDT) $(WORLD) $(TIME) $(QUF) include/quilt/cell.h include/quilt/proof.h include/quilt/route.h include/quilt/crdt.h include/quilt/world.h include/quilt/time.h include/quilt/quf.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRC)  -o $(BUILD)/engine.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(PROOF) -o $(BUILD)/proof.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(ROUTE) -o $(BUILD)/route.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(CRDT)  -o $(BUILD)/crdt.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(WORLD) -o $(BUILD)/world.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(TIME)  -o $(BUILD)/time.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(QUF)   -o $(BUILD)/quf.o
	ar rcs $@ $(BUILD)/engine.o $(BUILD)/proof.o $(BUILD)/route.o $(BUILD)/crdt.o $(BUILD)/world.o $(BUILD)/time.o $(BUILD)/quf.o

$(BUILD)/test_engine: $(TEST) $(SRC) include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST) $(SRC) -o $@

$(BUILD)/test_proof: $(TEST_PROOF) $(PROOF) $(SRC) include/quilt/proof.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_PROOF) $(PROOF) $(SRC) -o $@

$(BUILD)/test_route: $(TEST_ROUTE) $(ROUTE) $(SRC) include/quilt/route.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_ROUTE) $(ROUTE) $(SRC) -o $@

$(BUILD)/test_crdt: $(TEST_CRDT) $(CRDT) $(SRC) include/quilt/crdt.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_CRDT) $(CRDT) $(SRC) -o $@

$(BUILD)/test_world: $(TEST_WORLD) $(WORLD) include/quilt/world.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_WORLD) $(WORLD) -o $@

$(BUILD)/test_time: $(TEST_TIME) $(TIME) include/quilt/time.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_TIME) $(TIME) -o $@

$(BUILD)/test_quf: $(TEST_QUF) $(QUF) include/quilt/quf.h include/quilt/cell.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(TEST_QUF) $(QUF) -o $@

test: $(BUILD)/test_engine
	./$(BUILD)/test_engine

test-proof: $(BUILD)/test_proof
	./$(BUILD)/test_proof

test-route: $(BUILD)/test_route
	./$(BUILD)/test_route

test-crdt: $(BUILD)/test_crdt
	./$(BUILD)/test_crdt

test-world: $(BUILD)/test_world
	./$(BUILD)/test_world

test-time: $(BUILD)/test_time
	./$(BUILD)/test_time

test-quf: $(BUILD)/test_quf
	./$(BUILD)/test_quf

test-all: test test-proof test-route test-crdt test-world test-time test-quf

clean:
	rm -rf $(BUILD)
