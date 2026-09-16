/* quilt/world.h — the `physical.world` cell kind (Phase 222).
 *
 * Wraps the Code-as-World paradigm (MirroS-Lab, arXiv 2608.27549)
 * as a Quilt cell. The cell *is* a Python program that simulates
 * a physical scene; the cell's reads are the program's inputs;
 * the cell's value is the program's output.
 *
 * Why this fits Quilt:
 * - The cell model is: a struct (program) + a function (eval) +
 *   a list of dependents. Code-as-World is: a Python program
 *   (the code-as-world) + a Python interpreter (the eval) +
 *   the cells that read the simulation (the dependents).
 * - The 5+1+1+1+1 opcodes apply unchanged. BIND sets the program
 *   text. VIEW reads the simulation state. EFFECT re-executes.
 *   PROOF chain-anchors each BIND so a tampered program is detected.
 *   ROUTE picks the substrate (local Python, sandbox, or the
 *   Code-as-World-VL-9B model for synthesis).
 *
 * The polyformalism claim: the *shape* is the same in C and
 * Python; the substrate binding is what differs. In C we use
 * a small interpreter; in Python we use exec(); in the model
 * we use the abductive loop.
 */
#ifndef QUILT_WORLD_H
#define QUILT_WORLD_H

#include "quilt/cell.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── The 5 abductive-loop operations (from the paper) ──────────────── */
typedef enum {
    /* VLM proposes code from observation */
    QUILT_WORLD_PROPOSE = 0,
    /* The interpreter executes the proposed code */
    QUILT_WORLD_EXECUTE = 1,
    /* Render the simulation to an image */
    QUILT_WORLD_RENDER  = 2,
    /* Verify the simulation matches the observation */
    QUILT_WORLD_VERIFY  = 3,
    /* Refine the code (one abductive step) */
    QUILT_WORLD_REFINE  = 4,
    QUILT_WORLD__OPS_COUNT = 5
} quilt_world_op_t;

const char *quilt_world_op_name(quilt_world_op_t op);

/* ── A physical-quantity output ───────────────────────────────────── */
/* The paper evaluates on QuantiPhy: "Will the pendulum swing
 * past the post?". The answer is a number with uncertainty. */
typedef struct {
    double       value;        /* the scalar quantity (e.g. -2.3 m/s) */
    double       uncertainty;  /* the standard error */
    const char  *unit;         /* borrowed: "m", "m/s", "rad", "kg" */
    int          verified;     /* 1 if the simulation matches obs */
} quilt_quantity_t;

/* ── The cell's program (text) ──────────────────────────────────────── */
typedef struct {
    char        *code;         /* owned: the Python (or DSL) program */
    size_t       code_len;
    size_t       code_cap;
    int          n_inputs;     /* how many `reads` slots it expects */
    /* PROOF chain: every BIND of this cell appends an entry.
     * The state_hash is the FNV-1a of the program text. */
    uint8_t      prev_hash[32];
    uint8_t      state_hash[32];
    int          verified;     /* did the abductive loop verify? */
    /* The 5 operations emit append-only events: */
    int          n_propose;
    int          n_execute;
    int          n_render;
    int          n_verify;
    int          n_refine;
} quilt_world_program_t;

void quilt_world_program_init(quilt_world_program_t *p);
void quilt_world_program_free(quilt_world_program_t *p);
/* Set the program text. BIND in Quilt terms. */
int  quilt_world_program_set(quilt_world_program_t *p, const char *code);

/* ── The 5 abductive-loop operations ──────────────────────────────── */
int  quilt_world_propose(quilt_world_program_t *p, const char *code);
int  quilt_world_execute(quilt_world_program_t *p,
                           const quilt_value_t *reads, size_t n_reads,
                           quilt_quantity_t *out);
int  quilt_world_render(quilt_world_program_t *p, const char *image_path);
int  quilt_world_verify(quilt_world_program_t *p, double observed_value,
                          double tolerance);
/* The full abductive loop, one step: re-render + re-verify.
 * Returns 1 if verified, 0 if needs another iteration. */
int  quilt_world_refine(quilt_world_program_t *p,
                         const char *hint /* e.g. "object is heavier" */);

/* ── The polyformalism claim: the same cell in 3 forms ────────────── */
const char *quilt_world_kind_name(void);
int  quilt_world_kind_count(void);

#ifdef __cplusplus
}
#endif

#endif /* QUILT_WORLD_H */
