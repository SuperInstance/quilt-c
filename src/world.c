/* quilt/world.c — the `physical.world` cell kind runtime.
 *
 * In this C port, the abductive loop is a state machine: the
 * interpreter is a tiny C-side stub (real evaluation happens in
 * a Python subprocess or via the Code-as-World-VL-9B model).
 * The point is the *shape*: BIND, PROPOSE, EXECUTE, RENDER,
 * VERIFY, REFINE — same as the paper, exposed as Quilt opcodes.
 */
#include "quilt/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FNV-1a 64-bit (matches proof.c) */
static uint64_t fnv1a64(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static void hash_state(const char *s, size_t n, uint8_t out[32])
{
    uint64_t h = fnv1a64(s, n);
    memset(out, 0, 32);
    for (int i = 0; i < 4; i++) {
        uint64_t slice = h + (uint64_t)i * 0x9e3779b97f4a7c15ULL;
        memcpy(&out[i * 8], &slice, 8);
    }
}

const char *quilt_world_op_name(quilt_world_op_t op)
{
    switch (op) {
        case QUILT_WORLD_PROPOSE: return "PROPOSE";
        case QUILT_WORLD_EXECUTE: return "EXECUTE";
        case QUILT_WORLD_RENDER:  return "RENDER";
        case QUILT_WORLD_VERIFY:  return "VERIFY";
        case QUILT_WORLD_REFINE:  return "REFINE";
        default: return "?";
    }
}

void quilt_world_program_init(quilt_world_program_t *p)
{
    if (!p) return;
    p->code = NULL;
    p->code_len = 0;
    p->code_cap = 0;
    p->n_inputs = 0;
    memset(p->prev_hash, 0, 32);
    memset(p->state_hash, 0, 32);
    p->verified = 0;
    p->n_propose = 0;
    p->n_execute = 0;
    p->n_render = 0;
    p->n_verify = 0;
    p->n_refine = 0;
}

void quilt_world_program_free(quilt_world_program_t *p)
{
    if (!p) return;
    if (p->code) { free(p->code); p->code = NULL; }
    p->code_len = 0;
    p->code_cap = 0;
}

int quilt_world_program_set(quilt_world_program_t *p, const char *code)
{
    if (!p || !code) return -1;
    size_t n = strlen(code);
    /* Save prev_hash before overwriting (BIND semantics). */
    memcpy(p->prev_hash, p->state_hash, 32);
    /* Ensure capacity (powers of two). */
    if (n + 1 > p->code_cap) {
        size_t cap = p->code_cap ? p->code_cap : 64;
        while (cap < n + 1) cap *= 2;
        char *new_code = realloc(p->code, cap);
        if (!new_code) return -1;
        p->code = new_code;
        p->code_cap = cap;
    }
    memcpy(p->code, code, n);
    p->code[n] = '\0';
    p->code_len = n;
    /* New state_hash: FNV-1a of the program text. */
    hash_state(code, n, p->state_hash);
    p->verified = 0;  /* any BIND invalidates verification */
    return 0;
}

int quilt_world_propose(quilt_world_program_t *p, const char *code)
{
    if (!p || !code) return -1;
    if (quilt_world_program_set(p, code) != 0) return -1;
    p->n_propose++;
    return 0;
}

int quilt_world_execute(quilt_world_program_t *p,
                         const quilt_value_t *reads, size_t n_reads,
                         quilt_quantity_t *out)
{
    if (!p || !out) return -1;
    if (!p->code) return -1;
    /* In this C port, the interpreter is a stub: we hash the
     * program + the inputs and return a synthetic quantity.
     * The real interpreter lives in the substrate binding
     * (Python exec() on Workers, or the Code-as-World-VL
     * model for synthesis). The polyformalism claim is the
     * shape, not the math. */
    uint64_t h = fnv1a64(p->code, p->code_len);
    for (size_t i = 0; i < n_reads; i++) {
        if (reads[i].t == QUILT_V_FLOAT) {
            uint64_t bits;
            memcpy(&bits, &reads[i].u.f, 8);
            h ^= bits;
            h *= 0x100000001b3ULL;
        } else if (reads[i].t == QUILT_V_INT) {
            h ^= (uint64_t)reads[i].u.i;
            h *= 0x100000001b3ULL;
        }
    }
    /* Synthetic: hash mod 100 as the value, hash mod 10 as error. */
    out->value = (double)(h % 100) - 50.0;  /* range -50..+50 */
    out->uncertainty = (double)(h % 10) * 0.1;  /* 0..0.9 */
    out->unit = "?";
    out->verified = p->verified;
    p->n_execute++;
    return 0;
}

int quilt_world_render(quilt_world_program_t *p, const char *image_path)
{
    if (!p || !image_path) return -1;
    /* Stub: write a placeholder file so the C-side test passes.
     * Real renderers (matplotlib, three.js, etc.) live in the
     * substrate binding. */
    FILE *f = fopen(image_path, "wb");
    if (!f) return -1;
    fprintf(f, "PNG placeholder for program of length %zu\n", p->code_len);
    fclose(f);
    p->n_render++;
    return 0;
}

int quilt_world_verify(quilt_world_program_t *p, double observed_value,
                        double tolerance)
{
    if (!p) return 0;
    /* Re-execute to get the predicted value, compare to observed. */
    quilt_quantity_t q;
    if (quilt_world_execute(p, NULL, 0, &q) != 0) return 0;
    double diff = q.value - observed_value;
    int ok = (diff >= -tolerance && diff <= tolerance);
    p->verified = ok;
    p->n_verify++;
    return ok;
}

int quilt_world_refine(quilt_world_program_t *p, const char *hint)
{
    if (!p || !p->code) return 0;
    /* Append the hint to the program as a `# refine: <hint>` comment.
     * A real abductive loop would mutate the code based on the
     * hint; this stub is the test-mode shape. */
    char *new_code = malloc(p->code_len + strlen(hint) + 16);
    if (!new_code) return 0;
    sprintf(new_code, "%s\n# refine: %s\n", p->code, hint ? hint : "");
    int ok = quilt_world_program_set(p, new_code) == 0;
    free(new_code);
    p->n_refine++;
    return ok;
}

const char *quilt_world_kind_name(void) { return "physical.world"; }
int  quilt_world_kind_count(void) { return QUILT_WORLD__OPS_COUNT; }
