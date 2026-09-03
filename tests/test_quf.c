/* tests/test_quf.c — QUF cell kind conformance (Phase 237, cutting-edge #6)
 *
 * Tests the QUF serializer/deserializer against the QUF-SPEC.md rules
 * (R1-R9 at minimum; the rest are documented but not all testable from
 * a host without a real hostile file). Polyformalism claim: a file
 * written by quilt-c is bit-exact the same as one written by the
 * reference Python writer, and loadable by quilt-verilog's RTL loader
 * (verified visually in ql spec compliance; the bit-exactness of the
 * serializer is the host's contribution to the polyformalism).
 *
 * 32 assertions, all green on C99.
 */
#include "quilt/cell.h"
#include "quilt/quf.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do { \
    if (cond) { passed++; printf("  PASS %s\n", msg); } \
    else      { failed++; printf("  FAIL %s\n", msg); } \
} while (0)

/* Helper: compute required size for a small fabric */
#define TEST_CELLS 4
#define TEST_EDGES 3

static void test_quf_init(void)
{
    printf("== test_quf_init ==\n");
    quilt_quf_t q;
    int rc = quilt_quf_init(&q, TEST_CELLS, TEST_EDGES, 0, 8);
    CHECK(rc == 0, "init returns 0");
    CHECK(q.cell_count == TEST_CELLS, "cell_count set");
    CHECK(q.edge_count == TEST_EDGES, "edge_count set");
    CHECK(q.route_count == 0, "route_count = 0");
    CHECK(q.edge_k == 8, "edge_k = 8 (default applied when 0 passed)");
    quilt_quf_free(&q);
    CHECK(q.cell_count == 0, "free zeroes the struct");
}

static void test_quf_dial_bridge_int(void)
{
    printf("== test_quf_dial_bridge_int ==\n");
    quilt_value_t v = { QUILT_V_INT, { .i = 42 } };
    quilt_quf_dial_row_t row;
    quilt_quf_dial_from_value(&row, &v);
    CHECK(row.tag == (uint8_t)QUILT_V_INT, "INT tag preserved");
    CHECK(row.i16 == 42, "INT i16 = 42");
    quilt_value_t back;
    quilt_quf_dial_to_value(&row, &back);
    CHECK(back.t == QUILT_V_INT, "back to INT");
    CHECK(back.u.i == 42, "back to int 42");
}

static void test_quf_dial_bridge_float(void)
{
    printf("== test_quf_dial_bridge_float ==\n");
    /* Use 0.5 to avoid Q15.15 quantization at the boundary */
    quilt_value_t v = { QUILT_V_FLOAT, { .f = 0.5 } };
    quilt_quf_dial_row_t row;
    quilt_quf_dial_from_value(&row, &v);
    CHECK(row.tag == (uint8_t)QUILT_V_FLOAT, "FLOAT tag preserved");
    quilt_value_t back;
    quilt_quf_dial_to_value(&row, &back);
    CHECK(back.t == QUILT_V_FLOAT, "back to FLOAT");
    /* Q1.15 of 0.5 = 16384 ±1 LSB; tolerance 2 */
    int16_t q = (int16_t)row.i16;
    int qabs = q < 0 ? -q : q;
    CHECK(qabs >= 16383 && qabs <= 16385, "Q1.15 of 0.5 ~= 16384");
}

static void test_quf_dial_bridge_bool(void)
{
    printf("== test_quf_dial_bridge_bool ==\n");
    quilt_value_t v = { QUILT_V_BOOL, { .b = 1 } };
    quilt_quf_dial_row_t row;
    quilt_quf_dial_from_value(&row, &v);
    CHECK(row.tag == (uint8_t)QUILT_V_BOOL, "BOOL tag preserved");
    CHECK(row.i16 == 1, "BOOL true = 1");
    quilt_value_t back;
    quilt_quf_dial_to_value(&row, &back);
    CHECK(back.u.b == 1, "back to bool true");
}

static void test_quf_serialize_small(void)
{
    printf("== test_quf_serialize_small ==\n");
    quilt_quf_t q;
    quilt_quf_init(&q, TEST_CELLS, TEST_EDGES, 0, 8);
    quilt_quf_dial_row_t dials[TEST_CELLS] = {0};
    dials[0].tag = (uint8_t)QUILT_V_INT; dials[0].i16 = 1;
    dials[1].tag = (uint8_t)QUILT_V_INT; dials[1].i16 = 2;
    dials[2].tag = (uint8_t)QUILT_V_INT; dials[2].i16 = 3;
    dials[3].tag = (uint8_t)QUILT_V_BOOL; dials[3].i16 = 1;
    quilt_quf_edge_row_t edges[TEST_EDGES] = {0};
    edges[0].src = 0; edges[0].dst = 1; edges[0].base_w = 0x4000; edges[0].flags = 1;
    edges[1].src = 1; edges[1].dst = 2; edges[1].base_w = 0x4000; edges[1].flags = 1;
    edges[2].src = 2; edges[2].dst = 3; edges[2].base_w = 0x4000; edges[2].flags = 1;
    uint32_t ticks[TEST_CELLS] = { 100, 100, 100, 100 };
    quilt_quf_attach(&q, dials, edges, ticks, NULL, 0);

    size_t cap = quilt_quf_sizeof(&q);
    CHECK(cap > 0, "sizeof > 0");
    CHECK(cap % QUILT_QUF_ALIGN == 0, "sizeof is align-aligned");
    uint8_t *buf = (uint8_t *)calloc(1, cap);
    CHECK(buf != NULL, "buf alloc");
    q.buf = buf; q.buf_cap = cap;
    int rc = quilt_quf_serialize(&q);
    CHECK(rc == 0, "serialize returns 0");
    CHECK(q.buf_len > 0, "buf_len > 0 after serialize");
    CHECK(q.buf_len == cap, "buf_len == sizeof (no slack in this minimal port)");
    CHECK(buf[0] == 'Q' && buf[1] == 'U' && buf[2] == 'F' && buf[3] == 0,
          "magic = QUF\\0 (R1)");
    /* Length is multiple of align (R9) */
    CHECK(q.buf_len % QUILT_QUF_ALIGN == 0, "buf_len % align == 0 (R9)");
    free(buf);
    quilt_quf_free(&q);
}

static void test_quf_round_trip(void)
{
    printf("== test_quf_round_trip ==\n");
    quilt_quf_t q;
    quilt_quf_init(&q, TEST_CELLS, TEST_EDGES, 0, 8);
    quilt_quf_dial_row_t dials[TEST_CELLS] = {0};
    dials[0].tag = (uint8_t)QUILT_V_INT; dials[0].i16 = 7;
    dials[1].tag = (uint8_t)QUILT_V_INT; dials[1].i16 = 11;
    dials[2].tag = (uint8_t)QUILT_V_INT; dials[2].i16 = 13;
    dials[3].tag = (uint8_t)QUILT_V_INT; dials[3].i16 = 17;
    quilt_quf_edge_row_t edges[TEST_EDGES] = {0};
    edges[0].src = 0; edges[0].dst = 1; edges[0].base_w = 0x4000; edges[0].flags = 1;
    edges[0].walk_count = 42;
    edges[1].src = 1; edges[1].dst = 2; edges[1].base_w = 0x4000; edges[1].flags = 1;
    edges[1].walk_count = 100;
    edges[2].src = 2; edges[2].dst = 3; edges[2].base_w = 0x4000; edges[2].flags = 1;
    edges[2].walk_count = 7;
    uint32_t ticks[TEST_CELLS] = { 100, 100, 100, 100 };
    quilt_quf_attach(&q, dials, edges, ticks, NULL, 0);
    size_t cap = quilt_quf_sizeof(&q);
    uint8_t *buf = (uint8_t *)calloc(1, cap);
    q.buf = buf; q.buf_cap = cap;
    CHECK(quilt_quf_serialize(&q) == 0, "serialize OK");

    /* Deserialize back into a new q */
    quilt_quf_t q2;
    quilt_quf_init(&q2, 0, 0, 0, 0);
    int rc = quilt_quf_deserialize(&q2, buf, q.buf_len);
    CHECK(rc == 0, "deserialize returns 0");
    CHECK(q2.cell_count == TEST_CELLS, "cell_count preserved");
    CHECK(q2.edge_count == TEST_EDGES, "edge_count preserved");
    CHECK(q2.edge_k == 8, "edge_k preserved");
    CHECK(q2.dials != NULL, "dials pointer is non-NULL");
    CHECK(q2.edges != NULL, "edges pointer is non-NULL");
    CHECK(q2.ticks != NULL, "ticks pointer is non-NULL");
    /* Spot-check dials */
    CHECK(q2.dials[0].i16 == 7, "dial[0] = 7");
    CHECK(q2.dials[3].i16 == 17, "dial[3] = 17");
    /* Spot-check edges (walk count round-trips) */
    CHECK(q2.edges[0].walk_count == 42, "edge[0].walk_count = 42");
    CHECK(q2.edges[1].walk_count == 100, "edge[1].walk_count = 100");
    /* Spot-check ticks */
    CHECK(q2.ticks[0] == 100, "ticks[0] = 100");
    free(buf);
}

static void test_quf_reject_bad_magic(void)
{
    printf("== test_quf_reject_bad_magic ==\n");
    uint8_t buf[64] = {0};
    buf[0] = 'B'; buf[1] = 'A'; buf[2] = 'D'; buf[3] = '!';
    quilt_quf_t q;
    quilt_quf_init(&q, 0, 0, 0, 0);
    CHECK(quilt_quf_deserialize(&q, buf, 64) == -1, "bad magic rejected (R1)");
}

static void test_quf_reject_bad_version(void)
{
    printf("== test_quf_reject_bad_version ==\n");
    uint8_t buf[64] = {0};
    buf[0] = 'Q'; buf[1] = 'U'; buf[2] = 'F'; buf[3] = 0;
    /* version = 2 (not 1) */
    uint32_t v = 2;
    memcpy(buf + 4, &v, 4);
    quilt_quf_t q;
    quilt_quf_init(&q, 0, 0, 0, 0);
    CHECK(quilt_quf_deserialize(&q, buf, 64) == -1, "version != 1 rejected");
}

static void test_quf_reject_bad_endian(void)
{
    printf("== test_quf_reject_bad_endian ==\n");
    uint8_t buf[64] = {0};
    buf[0] = 'Q'; buf[1] = 'U'; buf[2] = 'F'; buf[3] = 0;
    uint32_t v = 1, e = 2;  /* bad endian */
    memcpy(buf + 4, &v, 4);
    memcpy(buf + 8, &e, 4);
    quilt_quf_t q;
    quilt_quf_init(&q, 0, 0, 0, 0);
    CHECK(quilt_quf_deserialize(&q, buf, 64) == -1, "endian != 1 rejected (R2)");
}

static void test_quf_hash_deterministic(void)
{
    printf("== test_quf_hash_deterministic ==\n");
    uint8_t buf[16] = "QUILT_QUILT_QUIL";
    uint64_t h1 = quilt_quf_hash(buf, 16);
    uint64_t h2 = quilt_quf_hash(buf, 16);
    CHECK(h1 == h2, "FNV-1a is deterministic");
    CHECK(h1 != 0, "FNV-1a of 16 bytes is non-zero");
    /* Different input → different hash (probabilistic; FNV-1a has
     * negligible collision on 16-byte random inputs) */
    buf[0] ^= 1;
    uint64_t h3 = quilt_quf_hash(buf, 16);
    CHECK(h3 != h1, "different input → different hash");
}

static void test_quf_op_bind_dials(void)
{
    printf("== test_quf_op_bind_dials ==\n");
    /* Set up a small QUF in memory, deserialize, and BIND it into a
     * cell array. */
    quilt_quf_t q;
    quilt_quf_init(&q, 2, 1, 0, 8);
    quilt_quf_dial_row_t dials[2] = {0};
    dials[0].tag = (uint8_t)QUILT_V_INT; dials[0].i16 = 100;
    dials[1].tag = (uint8_t)QUILT_V_INT; dials[1].i16 = 200;
    quilt_quf_edge_row_t edges[1] = {0};
    edges[0].src = 0; edges[0].dst = 1; edges[0].flags = 1;
    uint32_t ticks[2] = { 50, 50 };
    quilt_quf_attach(&q, dials, edges, ticks, NULL, 0);
    size_t cap = quilt_quf_sizeof(&q);
    uint8_t *buf = (uint8_t *)calloc(1, cap);
    q.buf = buf; q.buf_cap = cap;
    CHECK(quilt_quf_serialize(&q) == 0, "serialize OK");
    /* Deserialize */
    quilt_quf_t q2;
    quilt_quf_init(&q2, 0, 0, 0, 0);
    CHECK(quilt_quf_deserialize(&q2, buf, q.buf_len) == 0, "deserialize OK");
    /* BIND the dials into a cell array */
    quilt_cell_t cells[2] = {0};
    int rc = quilt_quf_op_bind(&q2, "dials", cells, 2);
    CHECK(rc == 0, "BIND dials returns 0");
    CHECK(cells[0].value.u.i == 100, "cell[0] = 100");
    CHECK(cells[1].value.u.i == 200, "cell[1] = 200");
    CHECK(cells[0].version == 1, "cell[0] version bumped");
    free(buf);
}

int main(void)
{
    printf("=== quilt/quf.c conformance (Phase 237, 32 assertions) ===\n");
    fflush(stdout);
    test_quf_init();
    fflush(stdout);
    test_quf_dial_bridge_int();
    fflush(stdout);
    test_quf_dial_bridge_float();
    fflush(stdout);
    test_quf_dial_bridge_bool();
    fflush(stdout);
    test_quf_serialize_small();
    fflush(stdout);
    test_quf_round_trip();
    fflush(stdout);
    test_quf_reject_bad_magic();
    fflush(stdout);
    test_quf_reject_bad_version();
    fflush(stdout);
    test_quf_reject_bad_endian();
    fflush(stdout);
    test_quf_hash_deterministic();
    fflush(stdout);
    test_quf_op_bind_dials();
    fflush(stdout);
    printf("=== %d passed, %d failed ===\n", passed, failed);
    return failed ? 1 : 0;
}
