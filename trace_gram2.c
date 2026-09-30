/* trace_gram2.c — Wave-69 P1 instrument: SCHEDULE-VARYING per-cell exercise traces.
 *
 * Lineage: trace_gram.c (wave-68 R3) had a FIXED call skeleton per cell —
 * seeds varied payloads only, so the opcode sequence was constant and the
 * `opsonly` variant degenerated (all 10 seeds identical). This instrument
 * repairs exactly that: the exercise SCHEDULE itself now varies.
 *
 * Schedule classes (3rd CLI arg, default 0):
 *   0 = original phase order (wave-68 baseline, reproducible)
 *   1 = reverse post-init phase order (init always first: UB-safe)
 *   2 = seeded random permutation of post-init phases (Fisher-Yates, rnd())
 *   3 = alternating front/back pick ("interleave")
 *
 * Design guarantees (declared, testable):
 *   - Step count per (cell, seed) is schedule-INVARIANT (same phases, same
 *     emits, reordered) => op 1-gram multiset is schedule-invariant BY
 *     CONSTRUCTION. Only order-sensitive features (2+ grams, edit distance,
 *     alignment) can see the schedule. This is the structural negative
 *     control for the wave-68 "embeddings are lexical bags" law.
 *   - Failed calls are honest telemetry (a phase run early may error —
 *     part of the run's behavior, per the wave-68 header contract).
 *   - rng_state mixes sched, so payload streams differ per schedule too.
 *
 * Build (from quilt-c checkout, same deps as trace_gram):
 *   cc -std=c99 -O2 -Iinclude trace_gram2.c -o build/trace_gram2 \
 *      build/libquilt-c.a src/route.c src/crdt.c src/time.c src/quf.c \
 *      src/world.c src/proof.c -lm
 * Usage: ./trace_gram2 <cell> <seed> <sched>
 * Emit: one JSON line per step: {cell,seed,sched,i,op,call,args,state,err}
 */
#include "quilt/cell.h"
#include "quilt/route.h"
#include "quilt/crdt.h"
#include "quilt/time.h"
#include "quilt/quf.h"
#include "quilt/world.h"
#include "quilt/proof.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *CELL = "?";
static int SEED = 0;
static int SCHED = 0;
static int STEP = 0;

static void emit(const char *op, const char *call, const char *args, const char *state, int err)
{
    printf("{\"cell\":\"%s\",\"seed\":%d,\"sched\":%d,\"i\":%d,\"op\":\"%s\",\"call\":\"%s\","
           "\"args\":\"%s\",\"state\":\"%s\",\"err\":%d}\n",
           CELL, SEED, SCHED, STEP++, op, call, args, state, err);
}

static unsigned long rng_state;
static unsigned rnd(unsigned n) { rng_state = rng_state * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)((rng_state >> 33) % n); }

#define MAXPH 8
typedef void (*phase_fn)(void);

/* run phases: ph[0] (init) fixed first, ph[n-1] (free, if present) fixed last,
 * middle phases reordered by the schedule class. Reordering never produces
 * use-after-free — only order-visible telemetry changes. */
static void run_phases(phase_fn *ph, int n)
{
    int mid = n - 2;                        /* permutable middle count */
    int order[MAXPH];
    if (n == 1) { ph[0](); return; }
    if (mid < 2) {                          /* nothing (or one middle) to reorder */
        for (int i = 0; i < n; i++) ph[i]();
        return;
    }
    for (int i = 0; i < mid; i++) order[i] = i;   /* local index into ph[1..n-2] */
    if (SCHED == 1) {                       /* reverse middle order */
        for (int i = 0; i < mid / 2; i++) { int t = order[i]; order[i] = order[mid - 1 - i]; order[mid - 1 - i] = t; }
    } else if (SCHED == 2) {                /* seeded Fisher-Yates */
        for (int i = mid - 1; i > 0; i--) { int j = (int)rnd((unsigned)(i + 1)); int t = order[i]; order[i] = order[j]; order[j] = t; }
    } else if (SCHED == 3) {                /* alternating front/back pick */
        int tmp[MAXPH], pick[MAXPH], front = 0, back = mid - 1;
        for (int i = 0; i < mid; i++) tmp[i] = order[i];
        for (int i = 0; i < mid; i++) pick[i] = (i % 2 == 0) ? tmp[front++] : tmp[back--];
        for (int i = 0; i < mid; i++) order[i] = pick[i];
    }
    ph[0]();                                /* init always first */
    for (int i = 0; i < mid; i++) ph[1 + order[i]]();
    ph[n - 1]();                            /* free/cleanup (or final op) always last */
}

/* ── route phases ──────────────────────────────────────────────────── */
static quilt_route_stats_t R;
static void ph_route_init(void) { emit("TICK", "route:init", "", "total=0", quilt_route_init(&R)); }
static void ph_route_records(void)
{
    const char *names[] = {"TEXT_LOG", "DENSE_VEC", "SPARSE_IDX", "KV_KEYVAL", "SYNTH_V2"};
    for (int i = 0; i < 12; i++) {
        int k = rnd(QUILT_ROUTE__COUNT);
        int rc = quilt_route_record(&R, (quilt_route_kind_t)k);
        char st[64]; snprintf(st, sizeof st, "total=%u count[%s]=%u", R.total, names[k], R.count[k]);
        emit("EFFECT", "route:record", names[k], st, rc < 0);
    }
}
static void ph_route_pick(void)
{
    quilt_route_kind_t pk = quilt_route_pick(&R);
    emit("VIEW", "route:pick", "", quilt_route_kind_name(pk), 0);
}
static void ph_route_policy(void)
{
    quilt_value_t v; v.t = QUILT_V_INT; v.u.i = (int64_t)rnd(1000);
    quilt_route_kind_t pol = quilt_route_policy(&v);
    char st[64]; snprintf(st, sizeof st, "v=%lld -> %s", (long long)v.u.i, quilt_route_kind_name(pol));
    emit("VIEW", "route:policy", "v:int", st, 0);
}

/* ── crdt phases ───────────────────────────────────────────────────── */
static quilt_pn_counter_t C;
static quilt_or_set_t S;
static quilt_mv_register_t M;
static void ph_crdt_init(void) { quilt_pn_counter_init(&C); quilt_or_set_init(&S); quilt_mv_register_init(&M); }
static void ph_crdt_pn(void)
{
    for (int i = 0; i < 8; i++) {
        int peer = (int)rnd(4);
        if (rnd(3)) { int rc = quilt_pn_counter_inc(&C, peer);
            char st[48]; snprintf(st, sizeof st, "peers[%d]++ value=%lld", peer, (long long)quilt_pn_counter_value(&C));
            emit("EFFECT", "crdt:pn_inc", "peer", st, rc); }
        else { int rc = quilt_pn_counter_dec(&C, peer);
            char st[48]; snprintf(st, sizeof st, "peers[%d]-- value=%lld", peer, (long long)quilt_pn_counter_value(&C));
            emit("EFFECT", "crdt:pn_dec", "peer", st, rc); }
    }
}
static void ph_crdt_orset(void)
{
    const char *elems[] = {"alpha", "beta", "gamma", "delta"};
    for (int i = 0; i < 4; i++) {
        int rc = quilt_or_set_add(&S, elems[i]);
        char st[48]; snprintf(st, sizeof st, "has[%s]=%d", elems[i], quilt_or_set_contains(&S, elems[i]));
        emit("BIND", "crdt:orset_add", elems[i], st, rc);
    }
}
static void ph_crdt_mv(void)
{
    quilt_value_t v; v.t = QUILT_V_INT; v.u.i = (int64_t)(rnd(500) - 250);
    emit("BIND", "crdt:mv_set", "v:int", "replica-local", quilt_mv_register_set(&M, v, 1, 2));
}

/* ── time phases ───────────────────────────────────────────────────── */
static quilt_time_cell_t T;
static double CTX[32];
static void ph_time_init(void)
{
    quilt_time_cell_init(&T);
    for (int i = 0; i < 32; i++) CTX[i] = (double)i + (double)rnd(10) / 10.0;
    emit("BIND", "time:bind_context", "ctx[32],n_variates=1", "context-bound", quilt_time_bind_context(&T, CTX, 32, 1));
}
static void ph_time_horizon(void)
{
    size_t hz = 3 + rnd(4);
    emit("BIND", "time:set_horizon", "horizon", "set", quilt_time_set_horizon(&T, hz));
}
static void ph_time_forecast(void)
{
    int rc = quilt_time_forecast(&T);
    emit("TICK", "time:forecast", "", "ran", rc);
}
static void ph_time_quantiles(void)
{
    for (int i = 0; i < 2; i++) {
        double pts[4];
        int rc2 = quilt_time_read_quantile(&T, 0.1 + 0.4 * i, i, pts, 4);
        char st[64]; snprintf(st, sizeof st, "q%.1f p0=%.2f p3=%.2f", 0.1 + 0.4 * i, pts[0], pts[3]);
        emit("VIEW", "time:read_quantile", "q,variate", st, rc2);
    }
}
static void ph_time_free(void) { quilt_time_cell_free(&T); }

/* ── quf phases ────────────────────────────────────────────────────── */
static quilt_quf_t Q;
static void ph_quf_init(void)
{
    int rc = quilt_quf_init(&Q, 4, 3, 1, 8);
    emit("TICK", "quf:init", "cells=4,edges=3", "init", rc);
}
static void ph_quf_dial(void)
{
    quilt_quf_dial_row_t row; memset(&row, 0, sizeof row);
    quilt_value_t v; v.t = QUILT_V_FLOAT; v.u.f = (rnd(2000) - 1000) / 100.0;
    quilt_quf_dial_from_value(&row, &v);
    quilt_value_t back; quilt_quf_dial_to_value(&row, &back);
    char st[64]; snprintf(st, sizeof st, "%.3f -> q115 -> %.3f", v.u.f, back.u.f);
    emit("EFFECT", "quf:dial_roundtrip", "v:float", st, 0);
}
static void ph_quf_hash(void)
{
    uint64_t h = quilt_quf_hash((const uint8_t *)"alpha-beta", 10);
    char st2[48]; snprintf(st2, sizeof st2, "hash=%016llx", (unsigned long long)h);
    emit("VIEW", "quf:hash", "buf[10]", st2, 0);
}
static void ph_quf_serialize(void)
{
    int rs = quilt_quf_serialize(&Q);
    emit("TICK", "quf:serialize", "", rs == 0 ? "ok" : "err", rs < 0);
}
static void ph_quf_free(void) { quilt_quf_free(&Q); }

/* ── world phases ──────────────────────────────────────────────────── */
static quilt_world_program_t W;
static void ph_world_init(void) { quilt_world_program_init(&W); }
static void ph_world_set(void)
{
    const char *code = "1 2 + 3 *";
    emit("BIND", "world:program_set", code, "loaded", quilt_world_program_set(&W, code));
}
static void ph_world_execute(void)
{
    quilt_quantity_t q = {0};
    int rc = quilt_world_execute(&W, NULL, 0, &q);
    char st[64]; snprintf(st, sizeof st, "quantity=%.3f", q.value);
    emit("TICK", "world:execute", "env=NULL", st, rc);
}
static void ph_world_verify(void)
{
    char st[64];
    double obs = (double)rnd(200) / 10.0 - 10.0;
    int rv = quilt_world_verify(&W, obs, 5.0);
    snprintf(st, sizeof st, "observed=%.2f tol=5", obs);
    emit("VIEW", "world:verify", "observed,tol", st, rv);
}
static void ph_world_free(void) { quilt_world_program_free(&W); }

/* ── proof phases ──────────────────────────────────────────────────── */
static quilt_proof_t P;
static quilt_proof_entry_t RING[QUILT_PROOF_RING_CAP];
static void ph_proof_init(void)
{
    quilt_proof_init(&P, RING);
    uint8_t sec[32]; memset(sec, 7, sizeof sec);
    emit("BIND", "proof:set_secret", "sec[32]", "set", quilt_proof_set_secret(&P, sec));
}
static void ph_proof_append(void)
{
    for (int i = 0; i < 4; i++) {
        quilt_value_t v; v.t = QUILT_V_INT; v.u.i = (int64_t)rnd(10000);
        int rc = quilt_proof_append(&P, &v, (uint64_t)i, 1);
        char st[48]; snprintf(st, sizeof st, "head=%u count=%u", P.head, P.count);
        emit("EFFECT", "proof:append", "v:int", st, rc);
    }
}
static void ph_proof_verify(void)
{
    int rv = quilt_proof_verify(&P);
    emit("VIEW", "proof:verify", "", rv == 0 ? "chain-ok" : "chain-bad", rv);
}

/* ── engine phases ─────────────────────────────────────────────────── */
static quilt_cell_t CELLS[8];
static quilt_engine_t E;
static const char *IDS[] = {"a", "b", "c", "d"};
static void ph_engine_init(void)
{
    int rc = quilt_engine_init(&E, CELLS, 8);
    emit("TICK", "engine:init", "cap=8", "ok", rc);
}
static void ph_engine_bind(void)
{
    for (int i = 0; i < 4; i++) {
        quilt_value_t v; v.t = QUILT_V_INT; v.u.i = (int64_t)rnd(100);
        int rb = quilt_bind(&E, IDS[i], v);
        char st[48]; snprintf(st, sizeof st, "%s=%lld", IDS[i], (long long)v.u.i);
        emit("BIND", "engine:bind", IDS[i], st, rb);
    }
}
static void ph_engine_link(void)
{
    for (int i = 0; i < 3; i++) {
        int rl = quilt_link(&E, IDS[i], IDS[i + 1]);
        char st[48]; snprintf(st, sizeof st, "%s->%s", IDS[i], IDS[i + 1]);
        emit("LINK", "engine:link", st, st, rl);
    }
}
static void ph_engine_effect(void)
{
    for (int i = 0; i < 2; i++) {
        int re = quilt_effect(&E, IDS[rnd(4)]);
        emit("EFFECT", "engine:effect", "id", "applied", re);
    }
}
static void ph_engine_view(void)
{
    for (int i = 0; i < 2; i++) {
        quilt_value_t out; int rv = quilt_view(&E, IDS[rnd(4)], &out);
        char st[48]; snprintf(st, sizeof st, "view=%s", rv == 0 ? "ok" : "missing");
        emit("VIEW", "engine:view", "id", st, rv);
    }
}
static void ph_engine_tick(void)
{
    uint64_t t = quilt_tick(&E);
    char st[48]; snprintf(st, sizeof st, "tick=%llu", (unsigned long long)t);
    emit("TICK", "engine:tick", "", st, 0);
}
static void ph_engine_forget(void)
{
    int rf = quilt_forget(&E, "d");
    emit("FORGET", "engine:forget", "d", "torn-down", rf);
}
static void ph_engine_free(void) { quilt_engine_free(&E); }

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <cell> <seed> [sched]\n", argv[0]); return 2; }
    CELL = argv[1];
    SEED = atoi(argv[2]);
    SCHED = (argc >= 4) ? atoi(argv[3]) : 0;
    rng_state = (unsigned long)SEED * 2654435761UL + (unsigned long)SCHED * 40503UL + 12345UL;

    if      (!strcmp(CELL, "route")) { phase_fn ph[] = {ph_route_init, ph_route_records, ph_route_pick, ph_route_policy}; run_phases(ph, 4); }
    else if (!strcmp(CELL, "crdt"))  { phase_fn ph[] = {ph_crdt_init, ph_crdt_pn, ph_crdt_orset, ph_crdt_mv}; run_phases(ph, 4); }
    else if (!strcmp(CELL, "time"))  { phase_fn ph[] = {ph_time_init, ph_time_horizon, ph_time_forecast, ph_time_quantiles, ph_time_free}; run_phases(ph, 5); }
    else if (!strcmp(CELL, "quf"))   { phase_fn ph[] = {ph_quf_init, ph_quf_dial, ph_quf_hash, ph_quf_serialize, ph_quf_free}; run_phases(ph, 5); }
    else if (!strcmp(CELL, "world")) { phase_fn ph[] = {ph_world_init, ph_world_set, ph_world_execute, ph_world_verify, ph_world_free}; run_phases(ph, 5); }
    else if (!strcmp(CELL, "proof")) { phase_fn ph[] = {ph_proof_init, ph_proof_append, ph_proof_verify}; run_phases(ph, 3); }
    else if (!strcmp(CELL, "engine")){ phase_fn ph[] = {ph_engine_init, ph_engine_bind, ph_engine_link, ph_engine_effect, ph_engine_view, ph_engine_tick, ph_engine_forget, ph_engine_free}; run_phases(ph, 8); }
    else { fprintf(stderr, "unknown cell\n"); return 2; }
    return 0;
}
