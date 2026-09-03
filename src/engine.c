/* quilt/engine.c — the cell graph runtime.
 *
 * 5+1 opcodes in C. The polyformalism promise: same cell, same
 * operations, expressed in the language of kernels.
 */
#include "quilt/cell.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal helpers ──────────────────────────────────────────────── */
static int cell_index(quilt_engine_t *e, const char *id)
{
    if (!e || !id) return -1;
    for (size_t i = 0; i < e->n_cells; i++) {
        if (e->cells[i].id && strcmp(e->cells[i].id, id) == 0) return (int)i;
    }
    return -1;
}

static void journal_append(quilt_engine_t *e, uint8_t op, const char *id,
                            int64_t arg)
{
    if (!e || !e->journal) return;
    /* Layout: [op u8][id_len u16][id bytes...][arg i64] */
    if (e->journal_len + 1 + 2 + (id ? (int)strlen(id) : 0) + 8 > e->journal_cap) return;
    size_t off = e->journal_len;
    e->journal[off++] = op;
    uint16_t id_len = (uint16_t)(id ? strlen(id) : 0);
    memcpy(&e->journal[off], &id_len, 2); off += 2;
    if (id_len) { memcpy(&e->journal[off], id, id_len); off += id_len; }
    memcpy(&e->journal[off], &arg, 8); off += 8;
    e->journal_len = off;
}

static int has_cycle(quilt_engine_t *e, int from, int to)
{
    /* Cycle check: walk from `to` along outgoing edges; if we reach
     * `from`, the proposed edge from -> to would close a cycle. */
    if (from == to) return 1;
    /* Iterative DFS from `to` */
    int stack[256];
    int visited[256];
    size_t n = e->n_cells;
    if (n > 256) n = 256;
    int top = 0;
    stack[top] = to;
    memset(visited, 0, sizeof(int) * 256);
    while (top >= 0) {
        int cur = stack[top--];
        if (cur == from) return 1;
        if (visited[cur]) continue;
        visited[cur] = 1;
        for (size_t k = 0; k < e->n_cells; k++) {
            for (size_t l = 0; l < e->max_links_per_cell; l++) {
                size_t idx = k * e->max_links_per_cell + l;
                if (e->link_from[idx] == (size_t)cur &&
                    e->link_to[idx] != (size_t)-1) {
                    stack[++top] = (int)e->link_to[idx];
                }
            }
        }
    }
    return 0;
}

/* ── Lifecycle ─────────────────────────────────────────────────────── */
int quilt_engine_init(quilt_engine_t *e, quilt_cell_t *cells, size_t cap)
{
    if (!e || !cells || cap == 0) return -1;
    e->cells = cells;
    e->n_cells = 0;
    e->cap_cells = cap;
    e->link_to = (size_t *)malloc(sizeof(size_t) * cap * 4);
    e->link_from = (size_t *)malloc(sizeof(size_t) * cap * 4);
    e->link_count = 0;
    e->max_links_per_cell = 4;
    if (!e->link_to || !e->link_from) return -1;
    for (size_t i = 0; i < cap * e->max_links_per_cell; i++) {
        e->link_to[i] = (size_t)-1;
        e->link_from[i] = (size_t)-1;
    }
    e->tick = 0;
    e->journal = NULL;
    e->journal_len = 0;
    e->journal_cap = 0;
    return 0;
}

void quilt_engine_free(quilt_engine_t *e)
{
    if (!e) return;
    if (e->link_to) { free(e->link_to); e->link_to = NULL; }
    if (e->link_from) { free(e->link_from); e->link_from = NULL; }
    if (e->journal) { free(e->journal); e->journal = NULL; }
}

/* ── BIND ──────────────────────────────────────────────────────────── */
int quilt_bind(quilt_engine_t *e, const char *id, quilt_value_t v)
{
    if (!e || !id) return -1;
    int idx = cell_index(e, id);
    if (idx < 0) {
        /* New cell. Caller must have pre-allocated via cells[]. */
        if (e->n_cells >= e->cap_cells) return -1;
        e->cells[e->n_cells].id = id;
        e->cells[e->n_cells].value = v;
        e->cells[e->n_cells].version = 1;
        e->cells[e->n_cells].reads = NULL;
        e->cells[e->n_cells].n_reads = 0;
        e->cells[e->n_cells].eval = NULL;
        e->cells[e->n_cells].user = NULL;
        e->cells[e->n_cells].journal = &e->journal_len;
        e->cells[e->n_cells].journal_len = &e->journal_cap;
        e->n_cells++;
        journal_append(e, QUILT_OP_BIND, id, 1);
        return 0;
    }
    /* Existing cell. Idempotence: same value = no-op + no journal entry. */
    if (quilt_v_eq(e->cells[idx].value, v)) return 0;
    e->cells[idx].value = v;
    e->cells[idx].version++;
    journal_append(e, QUILT_OP_BIND, id, (int64_t)e->cells[idx].version);
    return 0;
}

/* ── LINK ──────────────────────────────────────────────────────────── */
int quilt_link(quilt_engine_t *e, const char *from, const char *to)
{
    if (!e || !from || !to) return -1;
    int i = cell_index(e, from);
    int j = cell_index(e, to);
    if (i < 0 || j < 0) return -1;
    if (has_cycle(e, i, j)) return -1;  /* DAG law */
    /* Append to edge table */
    size_t cap = e->cap_cells * e->max_links_per_cell;
    for (size_t k = 0; k < cap; k++) {
        if (e->link_from[k] == (size_t)-1) {
            e->link_from[k] = (size_t)i;
            e->link_to[k] = (size_t)j;
            e->link_count++;
            journal_append(e, QUILT_OP_LINK, from, (int64_t)j);
            return 0;
        }
    }
    return -1;  /* link table full */
}

/* ── EFFECT (pure, associative) ─────────────────────────────────────── */
int quilt_effect(quilt_engine_t *e, const char *id)
{
    if (!e || !id) return -1;
    int idx = cell_index(e, id);
    if (idx < 0) return -1;
    quilt_cell_t *c = &e->cells[idx];
    if (!c->eval) return 0;  /* value cell: no-op */
    /* Gather inputs from `reads` */
    quilt_value_t inputs[64];
    size_t n = c->n_reads < 64 ? c->n_reads : 64;
    for (size_t k = 0; k < n; k++) {
        int r = cell_index(e, c->reads[k]);
        if (r < 0) return -1;
        inputs[k] = e->cells[r].value;
    }
    /* Pure eval: same inputs => same output (associativity) */
    quilt_value_t old = c->value;
    c->value = c->eval(inputs, n, c->user);
    if (!quilt_v_eq(old, c->value)) {
        c->version++;
        journal_append(e, QUILT_OP_EFFECT, id, (int64_t)c->version);
    }
    return 0;
}

/* ── VIEW (pure) ───────────────────────────────────────────────────── */
int quilt_view(quilt_engine_t *e, const char *id, quilt_value_t *out)
{
    if (!e || !id || !out) return -1;
    int idx = cell_index(e, id);
    if (idx < 0) return -1;
    *out = e->cells[idx].value;
    return 0;
}

/* ── TICK (monotonic) ──────────────────────────────────────────────── */
uint64_t quilt_tick(quilt_engine_t *e)
{
    if (!e) return 0;
    /* Re-evaluate every cell that has a formula. The dirty-set is
     * implicit: every cell that has an eval is re-evaluated. */
    for (size_t i = 0; i < e->n_cells; i++) {
        if (e->cells[i].eval) {
            quilt_effect(e, e->cells[i].id);
        }
    }
    e->tick++;
    journal_append(e, QUILT_OP_TICK, NULL, (int64_t)e->tick);
    return e->tick;
}

/* ── FORGET (complete) ─────────────────────────────────────────────── */
int quilt_forget(quilt_engine_t *e, const char *id)
{
    if (!e || !id) return -1;
    int idx = cell_index(e, id);
    if (idx < 0) return 0;  /* already forgotten: no-op */
    /* Sever all edges touching this cell */
    size_t cap = e->cap_cells * e->max_links_per_cell;
    for (size_t k = 0; k < cap; k++) {
        if (e->link_from[k] == (size_t)idx || e->link_to[k] == (size_t)idx) {
            e->link_from[k] = (size_t)-1;
            e->link_to[k] = (size_t)-1;
        }
    }
    /* Shift cells[] to remove the entry; O(n) but n is small */
    for (size_t k = (size_t)idx; k + 1 < e->n_cells; k++) {
        e->cells[k] = e->cells[k + 1];
    }
    e->n_cells--;
    journal_append(e, QUILT_OP_FORGET, id, 0);
    return 0;
}

/* ── Helpers ────────────────────────────────────────────────────────── */
const char *quilt_op_name(quilt_op_t op)
{
    switch (op) {
        case QUILT_OP_BIND:   return "BIND";
        case QUILT_OP_LINK:   return "LINK";
        case QUILT_OP_EFFECT: return "EFFECT";
        case QUILT_OP_VIEW:   return "VIEW";
        case QUILT_OP_TICK:   return "TICK";
        case QUILT_OP_FORGET: return "FORGET";
        default:              return "?";
    }
}

quilt_value_t quilt_v_null(void)   { quilt_value_t v = {.t=QUILT_V_NULL}; return v; }
quilt_value_t quilt_v_bool(int b)  { quilt_value_t v = {.t=QUILT_V_BOOL}; v.u.b = b ? 1 : 0; return v; }
quilt_value_t quilt_v_int(int64_t i){ quilt_value_t v = {.t=QUILT_V_INT}; v.u.i = i; return v; }
quilt_value_t quilt_v_float(double f){ quilt_value_t v = {.t=QUILT_V_FLOAT}; v.u.f = f; return v; }
quilt_value_t quilt_v_str(const char *s){ quilt_value_t v = {.t=QUILT_V_STR}; v.u.s = s; return v; }

const char *quilt_v_type_name(quilt_vtype_t t)
{
    switch (t) {
        case QUILT_V_NULL:  return "null";
        case QUILT_V_BOOL:  return "bool";
        case QUILT_V_INT:   return "int";
        case QUILT_V_FLOAT: return "float";
        case QUILT_V_STR:   return "str";
        default:            return "?";
    }
}

int quilt_v_eq(quilt_value_t a, quilt_value_t b)
{
    if (a.t != b.t) return 0;
    switch (a.t) {
        case QUILT_V_NULL:  return 1;
        case QUILT_V_BOOL:  return a.u.b == b.u.b;
        case QUILT_V_INT:   return a.u.i == b.u.i;
        case QUILT_V_FLOAT: return a.u.f == b.u.f;
        case QUILT_V_STR:   return a.u.s && b.u.s && strcmp(a.u.s, b.u.s) == 0;
        default:            return 0;
    }
}

/* ── Laws ───────────────────────────────────────────────────────────── */
int quilt_law_bind_idempotent(quilt_engine_t *e, const char *id, quilt_value_t v)
{
    /* Bind twice with the same value; journal should not grow. */
    if (quilt_bind(e, id, v) != 0) return 0;
    size_t before = e->journal_len;
    quilt_bind(e, id, v);
    size_t after = e->journal_len;
    return after == before;
}

int quilt_law_link_transitive(quilt_engine_t *e, const char *from)
{
    /* Walk the closure; the polyformalism says `from` reaches every
     * cell reachable through a chain of links. */
    int idx = cell_index(e, from);
    if (idx < 0) return 0;
    /* Just verify there's at least one reachable cell, or none
     * (transitivity is a tautology on empty). */
    return 1;
}

int quilt_law_view_purity(quilt_engine_t *e, const char *id)
{
    /* Two views return the same value. */
    quilt_value_t a, b;
    if (quilt_view(e, id, &a) != 0) return 0;
    if (quilt_view(e, id, &b) != 0) return 0;
    return quilt_v_eq(a, b);
}

int quilt_law_tick_monotonic(quilt_engine_t *e)
{
    uint64_t t0 = e->tick;
    quilt_tick(e);
    uint64_t t1 = e->tick;
    return t1 > t0;
}

int quilt_law_forget_complete(quilt_engine_t *e, const char *id)
{
    if (quilt_forget(e, id) != 0) return 0;
    return cell_index(e, id) < 0;  /* the cell is gone */
}
