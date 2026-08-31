/* quilt/cell.h — the minimum viable cell.
 *
 * "C thinks a cell is a struct. A struct with a function pointer.
 *  A struct with a list of dependents. That's it. The whole cell
 *  model fits in a few lines of C."  — README.md
 *
 * This header is the *complete* public API of the quilt-c polyformalism.
 * Six functions, one struct, no globals. 5+1 opcodes, expressed in
 * the C idiom. Target: C99, no allocations beyond the caller's
 * buffers, kernel-friendly, bare-metal-friendly.
 */
#ifndef QUILT_CELL_H
#define QUILT_CELL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opcodes (5+1) ──────────────────────────────────────────────────── */
typedef enum {
    QUILT_OP_BIND   = 0,  /* write a value to a cell (idempotent)   */
    QUILT_OP_LINK   = 1,  /* add a dependency edge (transitive)      */
    QUILT_OP_EFFECT = 2,  /* apply an effect to a cell (associative) */
    QUILT_OP_VIEW   = 3,  /* read a cell (pure, no mutation)         */
    QUILT_OP_TICK   = 4,  /* advance the engine one step (monotonic) */
    QUILT_OP_FORGET = 5,  /* tear down a cell (complete)             */
    QUILT_OP__COUNT = 6
} quilt_op_t;

/* ── Values ─────────────────────────────────────────────────────────── */
/* Quilt in C: scalars only, like MHS. The cell's value is a tagged
 * union; the polyformalism promise is the OPs, not the types. */
typedef enum {
    QUILT_V_NULL = 0,
    QUILT_V_BOOL,
    QUILT_V_INT,
    QUILT_V_FLOAT,
    QUILT_V_STR,
} quilt_vtype_t;

typedef struct {
    quilt_vtype_t t;
    union {
        int   b;
        int64_t i;
        double f;
        const char *s;   /* borrowed; cell doesn't own the string */
    } u;
} quilt_value_t;

/* ── Cells ──────────────────────────────────────────────────────────── */
typedef struct quilt_cell_s quilt_cell_t;
typedef struct quilt_engine_s quilt_engine_t;

/* Function-pointer shape: the cell kind. Each cell is "a struct
 * with a function pointer" — the function pointer IS the cell kind.
 *
 * evaluate() is the EFFECT path: given inputs (from cells in `reads`),
 * produce a new value. The polyformalism keeps it pure: same inputs
 * must always produce the same output (associativity).
 */
typedef quilt_value_t (*quilt_evaluator_t)(const quilt_value_t *inputs,
                                            size_t n_inputs,
                                            void *user);

/* One cell. "A struct with a function pointer and a list of dependents." */
struct quilt_cell_s {
    const char    *id;          /* borrowed; the cell's stable identity */
    quilt_value_t   value;       /* current value (BIND/VIEW) */
    const char   **reads;       /* NULL-terminated list of cell ids this depends on */
    size_t          n_reads;
    quilt_evaluator_t eval;      /* NULL for value cells; non-NULL for formula cells */
    void           *user;       /* user data passed to eval() */
    uint64_t        version;     /* monotonically increasing on every BIND */
    /* journal pointer (set by the engine; NULL on standalone cells) */
    uint64_t       *journal;
    size_t         *journal_len;
};

/* ── Engine (the cell graph) ─────────────────────────────────────────── */
struct quilt_engine_s {
    quilt_cell_t *cells;        /* contiguous array (caller-owned) */
    size_t        n_cells;
    size_t        cap_cells;
    /* adjacency: index → list of dependent indices (LINK edges) */
    size_t       *link_to;      /* edge targets, n_cells * max_links flattened */
    size_t       *link_from;    /* edge sources, same shape */
    size_t        link_count;
    size_t        max_links_per_cell;
    /* monotonic tick counter; ticks never decrease */
    uint64_t      tick;
    /* append-only journal of opcodes; the user owns the buffer */
    uint8_t      *journal;
    size_t        journal_len;
    size_t        journal_cap;
};

/* ── Lifecycle ──────────────────────────────────────────────────────── */
int  quilt_engine_init(quilt_engine_t *e, quilt_cell_t *cells, size_t cap);
void quilt_engine_free(quilt_engine_t *e);

/* ── Opcodes (the 5+1) ──────────────────────────────────────────────── */
/* All return 0 on success, -1 on error. */

/* BIND(name, value): idempotent by law. Rebinding the same id+value is
 * a no-op; rebinding with a different value updates and journals.
 * Cycles are detected and rejected. */
int  quilt_bind(quilt_engine_t *e, const char *id, quilt_value_t v);

/* LINK(from, to): add a dependency edge. Both cells must exist. Cycles
 * are rejected (BEFORE the link is added). */
int  quilt_link(quilt_engine_t *e, const char *from, const char *to);

/* EFFECT(id): evaluate the cell's formula against its reads. The eval
 * is pure: same inputs always produce the same output (associativity).
 * The engine caches by (id, version of each read). */
int  quilt_effect(quilt_engine_t *e, const char *id);

/* VIEW(id, out_value): pure read. Does not mutate state, does not
 * advance the tick. The function-pointer path of quilt_get. */
int  quilt_view(quilt_engine_t *e, const char *id, quilt_value_t *out);

/* TICK(): advance the engine one step. Increments the monotonic tick
 * counter and re-evaluates the dirty set. Returns the new tick count. */
uint64_t quilt_tick(quilt_engine_t *e);

/* FORGET(id): tear down a cell — remove the cell from the index,
 * sever its links, clear its journal entries. Complete by law: no
 * residue the laws can see. */
int  quilt_forget(quilt_engine_t *e, const char *id);

/* ── Helpers ────────────────────────────────────────────────────────── */
const char  *quilt_op_name(quilt_op_t op);
quilt_value_t quilt_v_null(void);
quilt_value_t quilt_v_bool(int b);
quilt_value_t quilt_v_int(int64_t i);
quilt_value_t quilt_v_float(double f);
quilt_value_t quilt_v_str(const char *s);
const char  *quilt_v_type_name(quilt_vtype_t t);
int  quilt_v_eq(quilt_value_t a, quilt_value_t b);   /* value equality */

/* ── Laws (cheap, observable from outside) ─────────────────────────── */
int  quilt_law_bind_idempotent(quilt_engine_t *e, const char *id,
                                quilt_value_t v);
int  quilt_law_link_transitive(quilt_engine_t *e, const char *from);
int  quilt_law_view_purity(quilt_engine_t *e, const char *id);
int  quilt_law_tick_monotonic(quilt_engine_t *e);
int  quilt_law_forget_complete(quilt_engine_t *e, const char *id);

#ifdef __cplusplus
}
#endif

#endif /* QUILT_CELL_H */
