/* quilt/route.c — the ROUTE effect runtime.
 *
 * The polyformalism claim is the routing table + the policy. The
 * substrate implementations are bound per-platform (Workers has
 * Vectorize; Cloudflare has D1; ESP32 has flash; CUDA has
 * device memory; the browser has IndexedDB).
 */
#include "quilt/route.h"

#include <string.h>

int quilt_route_init(quilt_route_stats_t *r)
{
    if (!r) return -1;
    for (int i = 0; i < QUILT_ROUTE__COUNT; i++) r->count[i] = 0;
    r->preferred = QUILT_ROUTE_TEXT_LOG;  /* safe default: journal */
    r->total = 0;
    return 0;
}

uint64_t quilt_route_record(quilt_route_stats_t *r,
                              quilt_route_kind_t kind)
{
    if (!r) return 0;
    if ((int)kind < 0 || (int)kind >= QUILT_ROUTE__COUNT) return r->total;
    if (r->count[kind] < UINT64_MAX) r->count[kind]++;
    r->total++;
    /* Recompute preferred */
    r->preferred = quilt_route_pick(r);
    return r->total;
}

quilt_route_kind_t quilt_route_pick(const quilt_route_stats_t *r)
{
    if (!r) return QUILT_ROUTE_TEXT_LOG;
    /* Argmax: pick the kind with the highest count. Ties are broken
     * by the lowest index (the first kind seen in the scan). We
     * start with best=0 and best_n=count[0] so the loop's strict
     * > comparator naturally prefers the earlier index on ties. */
    quilt_route_kind_t best = QUILT_ROUTE_DENSE_VEC;
    uint64_t best_n = r->count[best];
    for (int i = 1; i < QUILT_ROUTE__COUNT; i++) {
        if (r->count[i] > best_n) {
            best = (quilt_route_kind_t)i;
            best_n = r->count[i];
        }
    }
    return best;
}

const char *quilt_route_kind_name(quilt_route_kind_t k)
{
    switch (k) {
        case QUILT_ROUTE_DENSE_VEC:    return "DENSE_VEC";
        case QUILT_ROUTE_SPARSE_IDX:   return "SPARSE_IDX";
        case QUILT_ROUTE_TEXT_LOG:     return "TEXT_LOG";
        case QUILT_ROUTE_HIER_STORE:   return "HIER_STORE";
        case QUILT_ROUTE_PARAM_UPDATE: return "PARAM_UPDATE";
        default:                       return "?";
    }
}

quilt_route_kind_t quilt_route_policy(const quilt_value_t *v)
{
    if (!v) return QUILT_ROUTE_TEXT_LOG;
    switch (v->t) {
        case QUILT_V_NULL:  return QUILT_ROUTE_TEXT_LOG;
        case QUILT_V_BOOL:  return QUILT_ROUTE_PARAM_UPDATE;
        case QUILT_V_INT:   return QUILT_ROUTE_SPARSE_IDX;
        case QUILT_V_FLOAT: return QUILT_ROUTE_DENSE_VEC;
        case QUILT_V_STR:   return v->u.s && strlen(v->u.s) >= 256
                                     ? QUILT_ROUTE_DENSE_VEC
                                     : QUILT_ROUTE_HIER_STORE;
        default:            return QUILT_ROUTE_TEXT_LOG;
    }
}
