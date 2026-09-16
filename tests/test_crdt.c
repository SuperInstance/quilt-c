/* tests/test_crdt.c — Phase 218 CRDT conformance tests.
 *
 * State-based CRDTs (CvRDTs). Each is convergent: two replicas
 * that receive the same set of operations in any order reach the
 * same state. 5+1+1+1+1 opcodes.
 */
#include "quilt/cell.h"
#include "quilt/crdt.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do { \
    if (cond) { passed++; printf("  PASS %s\n", msg); } \
    else      { failed++; printf("  FAIL %s\n", msg); } \
} while (0)

/* ── PN-Counter tests ──────────────────────────────────────────────── */
static void test_pn_counter_basic(void)
{
    printf("== test_pn_counter_basic ==\n");
    quilt_pn_counter_t c;
    quilt_pn_counter_init(&c);
    CHECK(quilt_pn_counter_value(&c) == 0, "init: value = 0");
    quilt_pn_counter_inc(&c, 0);
    quilt_pn_counter_inc(&c, 0);
    quilt_pn_counter_inc(&c, 1);
    CHECK(quilt_pn_counter_value(&c) == 3, "after 3 inc: value = 3");
    quilt_pn_counter_dec(&c, 0);
    CHECK(quilt_pn_counter_value(&c) == 2, "after 1 dec: value = 2");
}

static void test_pn_counter_convergence(void)
{
    printf("== test_pn_counter_convergence ==\n");
    /* Two replicas. Same operations in different orders. Same value. */
    quilt_pn_counter_t a, b;
    quilt_pn_counter_init(&a);
    quilt_pn_counter_init(&b);
    /* Replica A sees: inc(0), inc(0), inc(1) */
    quilt_pn_counter_inc(&a, 0);
    quilt_pn_counter_inc(&a, 0);
    quilt_pn_counter_inc(&a, 1);
    /* Replica B sees: inc(1), inc(0), inc(0) */
    quilt_pn_counter_inc(&b, 1);
    quilt_pn_counter_inc(&b, 0);
    quilt_pn_counter_inc(&b, 0);
    CHECK(quilt_pn_counter_value(&a) == quilt_pn_counter_value(&b),
          "convergence: same ops in different order => same value");
    /* Merge and check */
    quilt_pn_counter_merge(&a, &b);
    CHECK(quilt_pn_counter_value(&a) == 3, "after merge: value = 3");
    quilt_pn_counter_merge(&b, &a);
    CHECK(quilt_pn_counter_value(&b) == 3, "after merge: value = 3 (b)");
}

/* ── MV-Register tests ─────────────────────────────────────────────── */
static void test_mv_register_basic(void)
{
    printf("== test_mv_register_basic ==\n");
    quilt_mv_register_t r;
    quilt_mv_register_init(&r);
    CHECK(quilt_mv_register_len(&r) == 0, "init: 0 values");
    quilt_mv_register_set(&r, quilt_v_int(42), 1, 0);
    CHECK(quilt_mv_register_len(&r) == 1, "1st set: 1 value");
    const quilt_value_t *v = quilt_mv_register_at(&r, 0);
    CHECK(v && v->u.i == 42, "value = 42");
}

static void test_mv_register_concurrent_writes(void)
{
    printf("== test_mv_register_concurrent_writes ==\n");
    /* Two replicas, each gets a write from a different peer at
     * the same clock. Both values survive. */
    quilt_mv_register_t a, b;
    quilt_mv_register_init(&a);
    quilt_mv_register_init(&b);
    quilt_mv_register_set(&a, quilt_v_int(1), 1, 0);
    quilt_mv_register_set(&b, quilt_v_int(2), 1, 1);
    CHECK(quilt_mv_register_len(&a) == 1, "a has 1 value");
    CHECK(quilt_mv_register_len(&b) == 1, "b has 1 value");
    /* Merge. Both should survive. */
    quilt_mv_register_merge(&a, &b);
    quilt_mv_register_merge(&b, &a);
    CHECK(quilt_mv_register_len(&a) == 2, "after merge: a has 2 concurrent values");
    CHECK(quilt_mv_register_len(&b) == 2, "after merge: b has 2 concurrent values");
    /* LWW: a higher clock from peer 0 overrides */
    quilt_mv_register_set(&a, quilt_v_int(99), 5, 0);
    quilt_mv_register_merge(&b, &a);
    CHECK(quilt_mv_register_len(&b) == 2, "b still 2 values (peer 0 -> 99, peer 1 -> 2)");
    int found_99 = 0, found_2 = 0;
    for (size_t i = 0; i < quilt_mv_register_len(&b); i++) {
        const quilt_value_t *v = quilt_mv_register_at(&b, i);
        if (v->u.i == 99) found_99 = 1;
        if (v->u.i == 2)  found_2 = 1;
    }
    CHECK(found_99, "b has 99 (peer 0, clock 5)");
    CHECK(found_2,  "b has 2 (peer 1, clock 1)");
}

/* ── OR-Set tests ──────────────────────────────────────────────────── */
static void test_or_set_basic(void)
{
    printf("== test_or_set_basic ==\n");
    quilt_or_set_t s;
    quilt_or_set_init(&s);
    CHECK(quilt_or_set_size(&s) == 0, "init: size = 0");
    quilt_or_set_add(&s, "alpha");
    quilt_or_set_add(&s, "beta");
    quilt_or_set_add(&s, "alpha");  /* duplicate: no-op */
    CHECK(quilt_or_set_size(&s) == 2, "after 2 unique adds: size = 2");
    CHECK(quilt_or_set_contains(&s, "alpha"), "contains alpha");
    CHECK(quilt_or_set_contains(&s, "beta"),  "contains beta");
    CHECK(!quilt_or_set_contains(&s, "gamma"), "does not contain gamma");
}

static void test_or_set_convergence(void)
{
    printf("== test_or_set_convergence ==\n");
    quilt_or_set_t a, b;
    quilt_or_set_init(&a);
    quilt_or_set_init(&b);
    quilt_or_set_add(&a, "x");
    quilt_or_set_add(&a, "y");
    quilt_or_set_add(&b, "y");
    quilt_or_set_add(&b, "z");
    /* Merge: x, y, z should be in both */
    quilt_or_set_merge(&a, &b);
    quilt_or_set_merge(&b, &a);
    CHECK(quilt_or_set_size(&a) == 3, "after merge: a has 3 elements");
    CHECK(quilt_or_set_size(&b) == 3, "after merge: b has 3 elements");
    CHECK(quilt_or_set_contains(&a, "x") &&
          quilt_or_set_contains(&a, "y") &&
          quilt_or_set_contains(&a, "z"),
          "a contains x, y, z");
    quilt_or_set_free(&a);
    quilt_or_set_free(&b);
}

/* ── Kind names ────────────────────────────────────────────────────── */
static void test_crdt_kind_names(void)
{
    printf("== test_crdt_kind_names ==\n");
    CHECK(strcmp(quilt_crdt_kind_name(QUILT_CRDT_COUNTER),  "PN_COUNTER")  == 0, "name(PN_COUNTER)");
    CHECK(strcmp(quilt_crdt_kind_name(QUILT_CRDT_REGISTER), "MV_REGISTER") == 0, "name(MV_REGISTER)");
    CHECK(strcmp(quilt_crdt_kind_name(QUILT_CRDT_SET),      "OR_SET")      == 0, "name(OR_SET)");
    CHECK(quilt_crdt_kind_count() == 3, "3 CRDT kinds");
}

int main(void)
{
    printf("=== quilt-c: CRDT opcodes (Phase 218 cutting-edge adoption #3) ===\n\n");
    test_pn_counter_basic();
    test_pn_counter_convergence();
    test_mv_register_basic();
    test_mv_register_concurrent_writes();
    test_or_set_basic();
    test_or_set_convergence();
    test_crdt_kind_names();
    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
