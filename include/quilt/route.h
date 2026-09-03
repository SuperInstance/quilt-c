/* quilt/route.h — the ROUTE effect (Phase 217 cutting-edge adoption #2).
 *
 * Idea: each Quilt cell can route its value to one of 5 memory
 * substrates. The polyformalism claim is the routing table, not the
 * substrate implementations. Adopted from:
 *
 *   - Harness-the-Memory (arXiv 2608.14302)
 *     "no single memory substrate dominates; long-context QA wants
 *      dense_vec, sequential decision-making wants scratchpad"
 *   - Hindsight / MemoryBank
 *     the value is the same; the substrate is the parameter
 *   - SmartCRDT
 *     state-based merge per substrate
 *
 * The 5 substrates:
 *   1. DENSE_VEC  — vector index (semantic recall; the embeddings)
 *   2. SPARSE_IDX — keyword index (BM25 / TF-IDF; the lookup)
 *   3. TEXT_LOG   — append-only text (the journal; provenance)
 *   4. HIER_STORE — hierarchical store (the cells' lineage; a tree)
 *   5. PARAM_UPDATE — gradient-style update (the weights; learning)
 *
 * The polyformalism pattern: each substrate is a function pointer
 * (like the cell kind). The router picks one per cell-state. The
 * same `quilt_value_t` flows through; the substrate choice is the
 * parameter.
 */
#ifndef QUILT_ROUTE_H
#define QUILT_ROUTE_H

#include "quilt/cell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QUILT_ROUTE_DENSE_VEC  = 0,  /* vector index */
    QUILT_ROUTE_SPARSE_IDX = 1,  /* keyword index */
    QUILT_ROUTE_TEXT_LOG   = 2,  /* append-only text */
    QUILT_ROUTE_HIER_STORE = 3,  /* hierarchical tree */
    QUILT_ROUTE_PARAM_UPDATE = 4, /* gradient update */
    QUILT_ROUTE__COUNT     = 5
} quilt_route_kind_t;

typedef struct {
    /* Per-substrate counters: how many times this cell has routed
     * to each substrate. Saturating at UINT64_MAX. */
    uint64_t count[QUILT_ROUTE__COUNT];
    /* The current preferred substrate for this cell. */
    quilt_route_kind_t preferred;
    /* The total routing events (sum of count[]). */
    uint64_t total;
} quilt_route_stats_t;

/* Initialize a route stats block. */
int  quilt_route_init(quilt_route_stats_t *r);

/* Record one routing event for this cell. Returns the new total. */
uint64_t quilt_route_record(quilt_route_stats_t *r,
                              quilt_route_kind_t kind);

/* Pick a substrate for a cell. Strategy: argmax of count[]. Ties
 * broken by lowest index. Returns the chosen kind. */
quilt_route_kind_t quilt_route_pick(const quilt_route_stats_t *r);

/* The 5 kind names, in the order of the enum. */
const char *quilt_route_kind_name(quilt_route_kind_t k);

/* The polyformalism policy: a cell's value is "long-context-shaped"
 * if its string length is >= 256 chars; "decision-shaped" if it's
 * a small int; "blend-shaped" otherwise. The policy returns the
 * substrate recommendation. (This is a sketch; real substrates
 * use richer heuristics.) */
quilt_route_kind_t quilt_route_policy(const quilt_value_t *v);

#ifdef __cplusplus
}
#endif

#endif /* QUILT_ROUTE_H */
