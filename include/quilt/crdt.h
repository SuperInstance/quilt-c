/* quilt/crdt.h — the CRDT opcode (Phase 218 cutting-edge adoption #3).
 *
 * Idea: state-based CRDT (CvRDT) per cell. Each cell is replicated
 * across N peers; the BIND op between cells becomes an op-CRDT
 * merge. The cowboy can fork a fleet of cells, mutate offline, and
 * converge on re-LINK without a central coordinator.
 *
 * Adopted from:
 *   - AgentRoom (arXiv 2608.23740): concurrent multi-agent in a
 *     CRDT-backed workspace
 *   - Electric (datomic/electric-sql): state-based CRDT
 *   - Loro: rich CRDT for collaborative text
 *   - Kleppmann's "local-first software"
 *
 * The polyformalism claim: a Quilt cell's value can be a CRDT
 * (PN-Counter, OR-Set, MV-Register). The same 5+1 opcodes apply;
 * the merge function is the substrate binding.
 */
#ifndef QUILT_CRDT_H
#define QUILT_CRDT_H

#include "quilt/cell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QUILT_CRDT_COUNTER   = 0,  /* PN-Counter: increment / decrement */
    QUILT_CRDT_REGISTER  = 1,  /* MV-Register: multi-value register */
    QUILT_CRDT_SET       = 2,  /* OR-Set: add / remove */
    QUILT_CRDT__COUNT    = 3
} quilt_crdt_kind_t;

typedef uint64_t quilt_lamport_t;

/* ── PN-Counter (state-based) ───────────────────────────────────────── */
typedef struct {
    int64_t   p[256];          /* per-peer positive counter */
    int64_t   n[256];          /* per-peer negative counter */
    quilt_lamport_t clock;
} quilt_pn_counter_t;

void quilt_pn_counter_init(quilt_pn_counter_t *c);
int  quilt_pn_counter_inc(quilt_pn_counter_t *c, int peer);
int  quilt_pn_counter_dec(quilt_pn_counter_t *c, int peer);
int64_t quilt_pn_counter_value(const quilt_pn_counter_t *c);
void quilt_pn_counter_merge(quilt_pn_counter_t *dst,
                            const quilt_pn_counter_t *src);

/* ── MV-Register (state-based) ─────────────────────────────────────── */
#define QUILT_MV_REG_MAX  8   /* max concurrent values */

typedef struct {
    quilt_value_t  values[QUILT_MV_REG_MAX];
    size_t         n_values;
    quilt_lamport_t clocks[QUILT_MV_REG_MAX];
    int            peers[QUILT_MV_REG_MAX];
} quilt_mv_register_t;

void quilt_mv_register_init(quilt_mv_register_t *r);
int  quilt_mv_register_set(quilt_mv_register_t *r, quilt_value_t v,
                            quilt_lamport_t clock, int peer);
size_t quilt_mv_register_len(const quilt_mv_register_t *r);
const quilt_value_t *quilt_mv_register_at(const quilt_mv_register_t *r, size_t i);
void quilt_mv_register_merge(quilt_mv_register_t *dst,
                              const quilt_mv_register_t *src);

/* ── OR-Set (state-based, simplified) ──────────────────────────────── */
#define QUILT_OR_SET_MAX  64

typedef struct {
    char  *adds[QUILT_OR_SET_MAX];   /* tombstones absent in this sketch */
    size_t n;
} quilt_or_set_t;

void quilt_or_set_init(quilt_or_set_t *s);
int  quilt_or_set_add(quilt_or_set_t *s, const char *elem);
int  quilt_or_set_contains(const quilt_or_set_t *s, const char *elem);
size_t quilt_or_set_size(const quilt_or_set_t *s);
void quilt_or_set_merge(quilt_or_set_t *dst, const quilt_or_set_t *src);
void quilt_or_set_free(quilt_or_set_t *s);

/* ── Kind helpers ──────────────────────────────────────────────────── */
const char *quilt_crdt_kind_name(quilt_crdt_kind_t k);
int quilt_crdt_kind_count(void);

#ifdef __cplusplus
}
#endif

#endif /* QUILT_CRDT_H */
