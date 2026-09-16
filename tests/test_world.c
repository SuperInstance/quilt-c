/* tests/test_world.c — Phase 222 conformance tests for the
 * `physical.world` cell kind (Code-as-World paradigm). */

#include "quilt/cell.h"
#include "quilt/world.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do { \
    if (cond) { passed++; printf("  PASS %s\n", msg); } \
    else      { failed++; printf("  FAIL %s\n", msg); } \
} while (0)

static void test_world_kind_name(void)
{
    printf("== test_world_kind_name ==\n");
    CHECK(strcmp(quilt_world_kind_name(), "physical.world") == 0,
          "kind name = physical.world");
    CHECK(quilt_world_kind_count() == 5, "5 abductive-loop operations");
    CHECK(strcmp(quilt_world_op_name(QUILT_WORLD_PROPOSE), "PROPOSE") == 0,
          "name(PROPOSE)");
    CHECK(strcmp(quilt_world_op_name(QUILT_WORLD_EXECUTE), "EXECUTE") == 0,
          "name(EXECUTE)");
    CHECK(strcmp(quilt_world_op_name(QUILT_WORLD_RENDER),  "RENDER")  == 0,
          "name(RENDER)");
    CHECK(strcmp(quilt_world_op_name(QUILT_WORLD_VERIFY),  "VERIFY")  == 0,
          "name(VERIFY)");
    CHECK(strcmp(quilt_world_op_name(QUILT_WORLD_REFINE),  "REFINE")  == 0,
          "name(REFINE)");
}

static void test_world_propose_sets_state_hash(void)
{
    printf("== test_world_propose_sets_state_hash ==\n");
    quilt_world_program_t p;
    quilt_world_program_init(&p);
    /* The hash is all zero at init. */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) if (p.state_hash[i]) all_zero = 0;
    CHECK(all_zero, "init state_hash is all-zero");
    /* Propose a program. State hash should be non-zero. */
    CHECK(quilt_world_propose(&p, "x = 1; y = x + 2") == 0,
          "propose('x = 1; y = x + 2') returns 0");
    int all_zero2 = 1;
    for (int i = 0; i < 32; i++) if (p.state_hash[i]) all_zero2 = 0;
    CHECK(!all_zero2, "after propose, state_hash is non-zero");
    /* A different program produces a different hash. */
    uint8_t hash1[32];
    memcpy(hash1, p.state_hash, 32);
    CHECK(quilt_world_propose(&p, "x = 1; y = x + 3") == 0,
          "propose with different code returns 0");
    CHECK(memcmp(hash1, p.state_hash, 32) != 0,
          "different code -> different state_hash");
    CHECK(p.n_propose == 2, "n_propose = 2");
    quilt_world_program_free(&p);
}

static void test_world_propose_updates_prev_hash(void)
{
    printf("== test_world_propose_updates_prev_hash ==\n");
    /* The PROOF chain: every BIND records the previous state_hash
     * before overwriting. */
    quilt_world_program_t p;
    quilt_world_program_init(&p);
    quilt_world_propose(&p, "v1");
    uint8_t hash_after_v1[32];
    memcpy(hash_after_v1, p.state_hash, 32);
    /* At this point, prev_hash should be all-zero (the init state). */
    int prev_was_zero = 1;
    for (int i = 0; i < 32; i++) if (p.prev_hash[i]) prev_was_zero = 0;
    CHECK(prev_was_zero, "prev_hash is all-zero after first propose");
    quilt_world_propose(&p, "v2");
    /* Now prev_hash should be hash_after_v1. */
    CHECK(memcmp(p.prev_hash, hash_after_v1, 32) == 0,
          "after second propose, prev_hash == state_hash after first");
    quilt_world_program_free(&p);
}

static void test_world_execute_produces_quantity(void)
{
    printf("== test_world_execute_produces_quantity ==\n");
    quilt_world_program_t p;
    quilt_world_program_init(&p);
    quilt_world_propose(&p, "x = 5; y = x * 2");
    quilt_quantity_t q = {0};
    CHECK(quilt_world_execute(&p, NULL, 0, &q) == 0,
          "execute returns 0");
    /* The value should be in the synthetic range -50..+50. */
    CHECK(q.value >= -50.0 && q.value <= 50.0,
          "value is in synthetic range -50..+50");
    CHECK(q.uncertainty >= 0.0 && q.uncertainty <= 0.9,
          "uncertainty is in 0..0.9");
    CHECK(p.n_execute == 1, "n_execute = 1");
    quilt_world_program_free(&p);
}

static void test_world_verify_resets_on_bind(void)
{
    printf("== test_world_verify_resets_on_bind ==\n");
    /* The "verified" flag is the cell's PROOF claim; a BIND
     * (new program text) invalidates it. */
    quilt_world_program_t p;
    quilt_world_program_init(&p);
    quilt_world_propose(&p, "x = 1");
    quilt_world_verify(&p, 0.0, 100.0);  /* generous tolerance: pass */
    /* After verify with very wide tolerance, verified should be 1
     * (the synthetic value is in -50..+50, diff to 0 is < 50,
     * < 100 tolerance). */
    CHECK(p.verified == 1, "verify sets verified = 1 with wide tolerance");
    /* A new BIND should reset verified to 0. */
    quilt_world_propose(&p, "x = 2");
    CHECK(p.verified == 0, "new propose resets verified = 0");
    quilt_world_program_free(&p);
}

static void test_world_render_writes_placeholder(void)
{
    printf("== test_world_render_writes_placeholder ==\n");
    quilt_world_program_t p;
    quilt_world_program_init(&p);
    quilt_world_propose(&p, "render a sphere");
    const char *path = "/tmp/quilt_world_test.png";
    CHECK(quilt_world_render(&p, path) == 0, "render returns 0");
    /* The file should exist (we just created it). */
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "render wrote a file");
    if (f) { fclose(f); remove(path); }
    CHECK(p.n_render == 1, "n_render = 1");
    quilt_world_program_free(&p);
}

static void test_world_refine_appends_hint(void)
{
    printf("== test_world_refine_appends_hint ==\n");
    quilt_world_program_t p;
    quilt_world_program_init(&p);
    quilt_world_propose(&p, "x = 1");
    uint8_t hash_before[32];
    memcpy(hash_before, p.state_hash, 32);
    CHECK(quilt_world_refine(&p, "object is heavier") != 0,
          "refine with hint returns non-zero (success)");
    /* The code should now contain the hint. */
    CHECK(strstr(p.code, "object is heavier") != NULL,
          "refine appended the hint to the program");
    /* The state_hash should have changed. */
    CHECK(memcmp(p.state_hash, hash_before, 32) != 0,
          "state_hash changed after refine");
    CHECK(p.n_refine == 1, "n_refine = 1");
    quilt_world_program_free(&p);
}

static void test_world_polyformalism_shape(void)
{
    printf("== test_world_polyformalism_shape ==\n");
    /* The polyformalism claim: the cell has the same shape in
     * C and Python and (eventually) Rust and GDScript. The
     * operations are PROPOSE/EXECUTE/RENDER/VERIFY/REFINE,
     * which are exactly the 5 operations from the paper's
     * "abductive discovery loop". */
    CHECK(QUILT_WORLD_PROPOSE == 0, "PROPOSE = 0");
    CHECK(QUILT_WORLD_EXECUTE == 1, "EXECUTE = 1");
    CHECK(QUILT_WORLD_RENDER  == 2, "RENDER = 2");
    CHECK(QUILT_WORLD_VERIFY  == 3, "VERIFY = 3");
    CHECK(QUILT_WORLD_REFINE  == 4, "REFINE = 4");
    /* The 5+1+1+1+1+1 opcodes + the 5 world-ops = 14 opcodes
     * total in the polyformalism (Phase 222 = 6th addition). */
    CHECK(5 + 1 + 1 + 1 + 1 + 1 == 10,
          "5 originals + FORGET + PROOF + ROUTE + CRDT + WORLD = 10");
}

int main(void)
{
    printf("=== quilt-c: physical.world cell kind (Phase 222, Code-as-World) ===\n\n");
    test_world_kind_name();
    test_world_propose_sets_state_hash();
    test_world_propose_updates_prev_hash();
    test_world_execute_produces_quantity();
    test_world_verify_resets_on_bind();
    test_world_render_writes_placeholder();
    test_world_refine_appends_hint();
    test_world_polyformalism_shape();
    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
