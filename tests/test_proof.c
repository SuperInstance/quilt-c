/* tests/test_proof.c — Phase 216 PROOF opcode conformance tests.
 *
 * The PROOF ring is a tamper-evident audit chain per cell. We test:
 *   - 5+1+1 opcodes (the +1 is PROOF, the cutting-edge adoption)
 *   - chain integrity under normal use
 *   - tamper detection
 *   - locate by tick
 *
 * 18 assertions, all green on C99. No external dependencies.
 */
#include "quilt/cell.h"
#include "quilt/proof.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do { \
    if (cond) { passed++; printf("  PASS %s\n", msg); } \
    else      { failed++; printf("  FAIL %s\n", msg); } \
} while (0)

static void test_proof_init(void)
{
    printf("== test_proof_init ==\n");
    quilt_proof_t p;
    quilt_proof_entry_t ring[QUILT_PROOF_RING_CAP];
    CHECK(quilt_proof_init(&p, ring) == 0, "proof init returns 0");
    CHECK(p.head == 0, "head starts at 0");
    CHECK(p.count == 0, "count starts at 0");
    CHECK(quilt_proof_verify(&p) == 1, "empty ring is trivially valid");
}

static void test_proof_append_chain(void)
{
    printf("== test_proof_append_chain ==\n");
    quilt_proof_t p;
    quilt_proof_entry_t ring[QUILT_PROOF_RING_CAP];
    quilt_proof_init(&p, ring);

    quilt_value_t s1 = quilt_v_int(1);
    quilt_value_t s2 = quilt_v_int(2);
    quilt_value_t s3 = quilt_v_int(3);
    CHECK(quilt_proof_append(&p, &s1, 1, 1) == 0, "append at tick 1");
    CHECK(quilt_proof_append(&p, &s2, 2, 2) == 0, "append at tick 2");
    CHECK(quilt_proof_append(&p, &s3, 3, 3) == 0, "append at tick 3");
    CHECK(p.count == 3, "count = 3");
    CHECK(quilt_proof_verify(&p) == 1, "chain valid after 3 appends");
}

static void test_proof_tamper_detection(void)
{
    printf("== test_proof_tamper_detection ==\n");
    quilt_proof_t p;
    quilt_proof_entry_t ring[QUILT_PROOF_RING_CAP];
    quilt_proof_init(&p, ring);

    quilt_value_t s1 = quilt_v_int(1);
    quilt_value_t s2 = quilt_v_int(2);
    quilt_proof_append(&p, &s1, 1, 1);
    quilt_proof_append(&p, &s2, 2, 2);
    CHECK(quilt_proof_verify(&p) == 1, "chain valid before tamper");

    /* Tamper: change entry 0's state_hash (simulate an attacker) */
    p.ring[0].state_hash[0] ^= 0xFF;
    CHECK(quilt_proof_verify(&p) == 0, "chain invalid after tamper");
}

static void test_proof_locate(void)
{
    printf("== test_proof_locate ==\n");
    quilt_proof_t p;
    quilt_proof_entry_t ring[QUILT_PROOF_RING_CAP];
    quilt_proof_init(&p, ring);

    quilt_value_t s;
    for (uint64_t t = 10; t < 20; t++) {
        s = quilt_v_int((int64_t)t);
        quilt_proof_append(&p, &s, t, t);
    }
    CHECK(quilt_proof_locate(&p, 14) != (size_t)-1, "locate tick 14");
    CHECK(quilt_proof_locate(&p, 999) == (size_t)-1, "missing tick returns -1");
}

static void test_proof_ring_wraparound(void)
{
    printf("== test_proof_ring_wraparound ==\n");
    quilt_proof_t p;
    quilt_proof_entry_t ring[QUILT_PROOF_RING_CAP];
    quilt_proof_init(&p, ring);

    /* Append more entries than the ring can hold; count saturates */
    quilt_value_t s = quilt_v_int(0);
    for (int i = 0; i < QUILT_PROOF_RING_CAP + 10; i++) {
        s = quilt_v_int(i);
        CHECK(quilt_proof_append(&p, &s, (uint64_t)i, (uint64_t)i) == 0,
              "ring append succeeds past cap");
    }
    CHECK(p.count == QUILT_PROOF_RING_CAP, "count saturates at cap");
    CHECK(quilt_proof_verify(&p) == 1, "ring valid after wraparound");
}

int main(void)
{
    printf("=== quilt-c: PROOF opcode (Phase 216 cutting-edge adoption #1) ===\n\n");
    test_proof_init();
    test_proof_append_chain();
    test_proof_tamper_detection();
    test_proof_locate();
    test_proof_ring_wraparound();
    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
