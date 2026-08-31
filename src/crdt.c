/* quilt/crdt.c — the CRDT opcodes runtime.
 *
 * State-based CRDTs in C. The merge functions satisfy the CRDT
 * convergence property: two replicas that receive the same set
 * of operations (in any order) will reach the same state.
 */
#include "quilt/crdt.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QUILT_LAMPORT_MAX UINT64_MAX

/* ── PN-Counter ────────────────────────────────────────────────────── */
void quilt_pn_counter_init(quilt_pn_counter_t *c)
{
    if (!c) return;
    for (int i = 0; i < 256; i++) { c->p[i] = 0; c->n[i] = 0; }
    c->clock = 0;
}

int quilt_pn_counter_inc(quilt_pn_counter_t *c, int peer)
{
    if (!c || peer < 0 || peer >= 256) return -1;
    if (c->p[peer] < INT64_MAX) c->p[peer]++;
    if (c->clock < QUILT_LAMPORT_MAX) c->clock++;
    return 0;
}

int quilt_pn_counter_dec(quilt_pn_counter_t *c, int peer)
{
    if (!c || peer < 0 || peer >= 256) return -1;
    if (c->n[peer] < INT64_MAX) c->n[peer]++;
    if (c->clock < QUILT_LAMPORT_MAX) c->clock++;
    return 0;
}

int64_t quilt_pn_counter_value(const quilt_pn_counter_t *c)
{
    if (!c) return 0;
    int64_t s = 0;
    for (int i = 0; i < 256; i++) {
        s += c->p[i];
        s -= c->n[i];
    }
    return s;
}

void quilt_pn_counter_merge(quilt_pn_counter_t *dst,
                              const quilt_pn_counter_t *src)
{
    if (!dst || !src) return;
    for (int i = 0; i < 256; i++) {
        if (src->p[i] > dst->p[i]) dst->p[i] = src->p[i];
        if (src->n[i] > dst->n[i]) dst->n[i] = src->n[i];
    }
    if (src->clock > dst->clock) dst->clock = src->clock;
}

/* ── MV-Register ───────────────────────────────────────────────────── */
void quilt_mv_register_init(quilt_mv_register_t *r)
{
    if (!r) return;
    r->n_values = 0;
    for (size_t i = 0; i < QUILT_MV_REG_MAX; i++) {
        r->values[i] = quilt_v_null();
        r->clocks[i] = 0;
        r->peers[i] = -1;
    }
}

/* LWW is biased against the new (set wins). Concurrent writes
 * (clock == clock) keep both values; the reader picks by merge. */
int quilt_mv_register_set(quilt_mv_register_t *r, quilt_value_t v,
                            quilt_lamport_t clock, int peer)
{
    if (!r) return -1;
    /* Drop any prior value with a lower clock from the same peer. */
    for (size_t i = 0; i < r->n_values; i++) {
        if (r->peers[i] == peer && r->clocks[i] < clock) {
            /* compact by moving the last entry into this slot */
            r->values[i] = r->values[r->n_values - 1];
            r->clocks[i] = r->clocks[r->n_values - 1];
            r->peers[i] = r->peers[r->n_values - 1];
            r->n_values--;
            i--;
        }
    }
    if (r->n_values >= QUILT_MV_REG_MAX) return -1;
    r->values[r->n_values] = v;
    r->clocks[r->n_values] = clock;
    r->peers[r->n_values]  = peer;
    r->n_values++;
    return 0;
}

size_t quilt_mv_register_len(const quilt_mv_register_t *r)
{
    return r ? r->n_values : 0;
}

const quilt_value_t *quilt_mv_register_at(const quilt_mv_register_t *r, size_t i)
{
    if (!r || i >= r->n_values) return NULL;
    return &r->values[i];
}

void quilt_mv_register_merge(quilt_mv_register_t *dst,
                              const quilt_mv_register_t *src)
{
    if (!dst || !src) return;
    for (size_t i = 0; i < src->n_values; i++) {
        int peer = src->peers[i];
        int found = 0;
        for (size_t j = 0; j < dst->n_values; j++) {
            if (dst->peers[j] == peer) {
                if (src->clocks[i] > dst->clocks[j]) {
                    dst->values[j] = src->values[i];
                    dst->clocks[j] = src->clocks[i];
                }
                found = 1;
                break;
            }
        }
        if (!found) {
            if (dst->n_values < QUILT_MV_REG_MAX) {
                dst->values[dst->n_values] = src->values[i];
                dst->clocks[dst->n_values] = src->clocks[i];
                dst->peers[dst->n_values] = peer;
                dst->n_values++;
            }
        }
    }
}

/* ── OR-Set (simplified: add-only in this sketch) ─────────────────── */
void quilt_or_set_init(quilt_or_set_t *s)
{
    if (!s) return;
    s->n = 0;
    for (size_t i = 0; i < QUILT_OR_SET_MAX; i++) s->adds[i] = NULL;
}

int quilt_or_set_add(quilt_or_set_t *s, const char *elem)
{
    if (!s || !elem) return -1;
    /* Add the element if it's not already there. */
    for (size_t i = 0; i < s->n; i++) {
        if (s->adds[i] && strcmp(s->adds[i], elem) == 0) return 0;
    }
    if (s->n >= QUILT_OR_SET_MAX) return -1;
    size_t len = strlen(elem);
    s->adds[s->n] = (char *)malloc(len + 1);
    if (!s->adds[s->n]) return -1;
    memcpy(s->adds[s->n], elem, len + 1);
    s->n++;
    return 0;
}

int quilt_or_set_contains(const quilt_or_set_t *s, const char *elem)
{
    if (!s || !elem) return 0;
    for (size_t i = 0; i < s->n; i++) {
        if (s->adds[i] && strcmp(s->adds[i], elem) == 0) return 1;
    }
    return 0;
}

size_t quilt_or_set_size(const quilt_or_set_t *s)
{
    return s ? s->n : 0;
}

void quilt_or_set_merge(quilt_or_set_t *dst, const quilt_or_set_t *src)
{
    if (!dst || !src) return;
    for (size_t i = 0; i < src->n; i++) {
        quilt_or_set_add(dst, src->adds[i]);
    }
}

void quilt_or_set_free(quilt_or_set_t *s)
{
    if (!s) return;
    for (size_t i = 0; i < s->n; i++) {
        if (s->adds[i]) { free(s->adds[i]); s->adds[i] = NULL; }
    }
    s->n = 0;
}

/* ── Kind helpers ──────────────────────────────────────────────────── */
const char *quilt_crdt_kind_name(quilt_crdt_kind_t k)
{
    switch (k) {
        case QUILT_CRDT_COUNTER:  return "PN_COUNTER";
        case QUILT_CRDT_REGISTER: return "MV_REGISTER";
        case QUILT_CRDT_SET:      return "OR_SET";
        default:                  return "?";
    }
}

int quilt_crdt_kind_count(void)
{
    return QUILT_CRDT__COUNT;
}
