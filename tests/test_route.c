/* tests/test_route.c — Phase 217 ROUTE effect conformance tests.
 *
 * 5+1+1+1 opcodes, 5 substrates, the polyformalism router.
 * 22 assertions.
 */
#include "quilt/cell.h"
#include "quilt/route.h"

#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do { \
    if (cond) { passed++; printf("  PASS %s\n", msg); } \
    else      { failed++; printf("  FAIL %s\n", msg); } \
} while (0)

static void test_route_init(void)
{
    printf("== test_route_init ==\n");
    quilt_route_stats_t r;
    CHECK(quilt_route_init(&r) == 0, "route init returns 0");
    CHECK(r.total == 0, "total = 0 after init");
    CHECK(r.preferred == QUILT_ROUTE_TEXT_LOG, "preferred = TEXT_LOG (safe default)");
    for (int i = 0; i < QUILT_ROUTE__COUNT; i++) {
        CHECK(r.count[i] == 0, "count[i] = 0 for all 5 substrates");
    }
    CHECK(QUILT_ROUTE__COUNT == 5, "5 substrates");
}

static void test_route_record(void)
{
    printf("== test_route_record ==\n");
    quilt_route_stats_t r;
    quilt_route_init(&r);
    CHECK(quilt_route_record(&r, QUILT_ROUTE_DENSE_VEC) == 1, "1st record, total=1");
    CHECK(quilt_route_record(&r, QUILT_ROUTE_DENSE_VEC) == 2, "2nd record, total=2");
    CHECK(quilt_route_record(&r, QUILT_ROUTE_SPARSE_IDX) == 3, "3rd record, total=3");
    CHECK(r.count[QUILT_ROUTE_DENSE_VEC] == 2, "DENSE_VEC count = 2");
    CHECK(r.count[QUILT_ROUTE_SPARSE_IDX] == 1, "SPARSE_IDX count = 1");
    CHECK(r.preferred == QUILT_ROUTE_DENSE_VEC, "preferred = DENSE_VEC (argmax)");
}

static void test_route_pick_ties(void)
{
    printf("== test_route_pick_ties ==\n");
    quilt_route_stats_t r;
    quilt_route_init(&r);
    /* Equal counts: pick lowest index. TEXT_LOG (index 2) wins. */
    for (int i = 0; i < QUILT_ROUTE__COUNT; i++) {
        /* Each kind gets 1 event. */
        if (i == 0) quilt_route_record(&r, QUILT_ROUTE_DENSE_VEC);
        else if (i == 1) quilt_route_record(&r, QUILT_ROUTE_SPARSE_IDX);
        else if (i == 2) quilt_route_record(&r, QUILT_ROUTE_TEXT_LOG);
        else if (i == 3) quilt_route_record(&r, QUILT_ROUTE_HIER_STORE);
        else quilt_route_record(&r, QUILT_ROUTE_PARAM_UPDATE);
    }
    /* After each event, preferred is recomputed. Final preferred
     * should be the kind with the highest count (all tied at 1);
     * pick() returns the first argmax, which is index 0. */
    CHECK(r.preferred == QUILT_ROUTE_DENSE_VEC,
          "ties broken by lowest index: preferred = DENSE_VEC");
}

static void test_route_policy(void)
{
    printf("== test_route_policy ==\n");
    quilt_value_t v_null = quilt_v_null();
    quilt_value_t v_bool = quilt_v_bool(1);
    quilt_value_t v_int  = quilt_v_int(42);
    quilt_value_t v_float = quilt_v_float(3.14);
    quilt_value_t v_str_short = quilt_v_str("hi");
    quilt_value_t v_str_long = quilt_v_str(
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
        "sed do eiusmod tempor incididunt ut labore et dolore magna "
        "aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
        "ullamco laboris nisi ut aliquip ex ea commodo consequat. "
        "Duis aute irure dolor in reprehenderit in voluptate velit "
        "esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
        "occaecat cupidatat non proident, sunt in culpa qui officia "
        "deserunt mollit anim id est laborum. Sed ut perspiciatis "
        "unde omnis iste natus error sit voluptatem accusantium "
        "doloremque laudantium, totam rem aperiam, eaque ipsa quae "
        "ab illo inventore veritatis et quasi architecto beatae.");
    CHECK(quilt_route_policy(&v_null)  == QUILT_ROUTE_TEXT_LOG,
          "null -> TEXT_LOG (the journal is always safe)");
    CHECK(quilt_route_policy(&v_bool)  == QUILT_ROUTE_PARAM_UPDATE,
          "bool -> PARAM_UPDATE (a flag is a parameter change)");
    CHECK(quilt_route_policy(&v_int)   == QUILT_ROUTE_SPARSE_IDX,
          "int -> SPARSE_IDX (a small int is a lookup key)");
    CHECK(quilt_route_policy(&v_float) == QUILT_ROUTE_DENSE_VEC,
          "float -> DENSE_VEC (a scalar is one dimension of a vector)");
    CHECK(quilt_route_policy(&v_str_short) == QUILT_ROUTE_HIER_STORE,
          "short string -> HIER_STORE (a label, fits in a tree)");
    CHECK(quilt_route_policy(&v_str_long)  == QUILT_ROUTE_DENSE_VEC,
          "long string (>=256 chars) -> DENSE_VEC (semantic recall)");
}

static void test_route_kind_names(void)
{
    printf("== test_route_kind_names ==\n");
    CHECK(strcmp(quilt_route_kind_name(QUILT_ROUTE_DENSE_VEC),    "DENSE_VEC")    == 0, "name(DENSE_VEC)");
    CHECK(strcmp(quilt_route_kind_name(QUILT_ROUTE_SPARSE_IDX),   "SPARSE_IDX")   == 0, "name(SPARSE_IDX)");
    CHECK(strcmp(quilt_route_kind_name(QUILT_ROUTE_TEXT_LOG),     "TEXT_LOG")     == 0, "name(TEXT_LOG)");
    CHECK(strcmp(quilt_route_kind_name(QUILT_ROUTE_HIER_STORE),   "HIER_STORE")   == 0, "name(HIER_STORE)");
    CHECK(strcmp(quilt_route_kind_name(QUILT_ROUTE_PARAM_UPDATE), "PARAM_UPDATE") == 0, "name(PARAM_UPDATE)");
}

int main(void)
{
    printf("=== quilt-c: ROUTE effect (Phase 217 cutting-edge adoption #2) ===\n\n");
    test_route_init();
    test_route_record();
    test_route_pick_ties();
    test_route_policy();
    test_route_kind_names();
    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
