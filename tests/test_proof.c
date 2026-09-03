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

static void test_proof_hash_distinct(void)
{
    printf("== test_proof_hash_distinct ==\n");
    /* Different values must hash to different state_hashes. */
    quilt_proof_t p;
    quilt_proof_entry_t ring[8];
    quilt_proof_init(&p, ring);
    /* Two values: 1 and 2. Their state_hashes must differ. */
    quilt_value_t v1 = quilt_v_int(1);
    quilt_value_t v2 = quilt_v_int(2);
    quilt_value_t v3 = quilt_v_bool(0);
    quilt_proof_append(&p, &v1, 1, 1);
    quilt_proof_append(&p, &v2, 2, 2);
    quilt_proof_append(&p, &v3, 3, 3);
    CHECK(memcmp(p.ring[0].state_hash, p.ring[1].state_hash, 32) != 0,
          "int(1) and int(2) hash differently");
    CHECK(memcmp(p.ring[0].state_hash, p.ring[2].state_hash, 32) != 0,
          "int(1) and bool(0) hash differently");
    CHECK(quilt_proof_verify(&p) == 1, "chain still valid");
}

static void test_proof_with_secret(void)
{
    printf("== test_proof_with_secret ==\n");
    /* With a secret, every entry's sig is non-zero. */
    quilt_proof_t p;
    quilt_proof_entry_t ring[8];
    quilt_proof_init(&p, ring);
    uint8_t sec[32];
    for (int i = 0; i < 32; i++) sec[i] = (uint8_t)(i + 7);
    CHECK(quilt_proof_set_secret(&p, sec) == 0, "set secret");
    quilt_value_t s = quilt_v_int(42);
    quilt_proof_append(&p, &s, 1, 1);
    /* sig should be non-zero (HMAC) */
    int sig_nonzero = 0;
    for (int i = 0; i < 64; i++) if (p.ring[0].sig[i]) sig_nonzero = 1;
    CHECK(sig_nonzero, "sig is non-zero with secret");
    CHECK(quilt_proof_verify(&p) == 1, "chain valid (prev_hash link)");
    CHECK(quilt_proof_verify_full(&p) == 1, "chain valid (full verify incl. sigs)");
}

static void test_proof_sig_tamper(void)
{
    printf("== test_proof_sig_tamper ==\n");
    /* Tampering with a sig must fail verify_full. */
    quilt_proof_t p;
    quilt_proof_entry_t ring[8];
    quilt_proof_init(&p, ring);
    uint8_t sec[32];
    for (int i = 0; i < 32; i++) sec[i] = (uint8_t)(i * 3 + 1);
    quilt_proof_set_secret(&p, sec);
    quilt_value_t s = quilt_v_int(100);
    quilt_proof_append(&p, &s, 1, 1);
    quilt_proof_append(&p, &s, 2, 2);
    CHECK(quilt_proof_verify_full(&p) == 1, "valid before tamper");
    /* Tamper entry 0's sig */
    p.ring[0].sig[0] ^= 0xFF;
    CHECK(quilt_proof_verify_full(&p) == 0, "verify_full fails after sig tamper");
    /* But the prev_hash chain is still intact */
    CHECK(quilt_proof_verify(&p) == 1, "prev_hash chain still valid");
}

int main(void)
{
    printf("=== quilt-c: PROOF opcode (Phase 216 cutting-edge adoption #1) ===\n\n");
    test_proof_init();
    test_proof_append_chain();
    test_proof_tamper_detection();
    test_proof_locate();
    test_proof_ring_wraparound();
    test_proof_hash_distinct();
    test_proof_with_secret();
    test_proof_sig_tamper();
    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
