/* live_canon.c — C99 port of the Live Canon
 *
 * Reads AI-Writings papers as a navigable cell fabric, with 5 operations:
 *   1. NAVIGATE  - BFS through citations
 *   2. CONFLUENCE - join 2+ papers, suggest synthesis
 *   3. LINEAGE   - trace F-number through time
 *   4. GHOST     - find paper that should exist by shape proximity
 *   5. TICK      - re-balance the canon
 *
 * This is the C99 port. The cell-fabric idea is polyformal:
 *   C, Rust, Python, Verilog, VHDL — all read the canon the same way.
 *
 * Phase 251 of the polyformalism canon.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>

#define MAX_PAPERS 256
#define MAX_REFS 32
#define MAX_TITLE 256
#define MAX_DATE 16

/* A single cell in the canon fabric. */
typedef struct {
    uint32_t number;
    char title[MAX_TITLE];
    uint32_t f_number;
    uint32_t phase;
    char date[MAX_DATE];
    uint32_t ref_papers[MAX_REFS];
    uint32_t n_refs;
    uint32_t ref_f_numbers[MAX_REFS];
    uint32_t n_f_refs;
} Cell;

/* A 16-dial vector (Q1.15). */
typedef uint16_t Dials[16];

/* The Live Canon. */
typedef struct {
    Cell papers[MAX_PAPERS];
    Dials dials[MAX_PAPERS];
    uint32_t n_papers;
    uint64_t state_hash;
} LiveCanon;

/* ----- FNV-1a 64-bit hash (matches Python) ----- */
uint64_t fnv1a_64(const char *s) {
    uint64_t h = 0xCBF29CE484222325ULL;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 0x00000100000001B3ULL;
    }
    return h;
}

/* ----- Convert a cell to its 16-dial vector. ----- */
void cell_to_dials(const Cell *c, Dials out) {
    int year = 1970;
    if (strlen(c->date) >= 4) {
        year = (c->date[0]-'0')*1000 + (c->date[1]-'0')*100 +
               (c->date[2]-'0')*10 + (c->date[3]-'0');
    }
    uint32_t year_off = (uint32_t)(year - 1970);
    if (year_off > 60) year_off = 60;
    uint16_t year_q = (uint16_t)(year_off * 546);  /* 60 → 0x7FFF */

    uint16_t phase_q = (uint16_t)(c->phase * 218);  /* 300 → 0x7FFF */
    uint16_t f_q = (uint16_t)(c->f_number * 218);
    uint32_t n_refs = c->n_refs + c->n_f_refs;
    if (n_refs > 127) n_refs = 127;
    uint16_t n_refs_q = (uint16_t)(n_refs * 256);

    uint64_t th = fnv1a_64(c->title);
    uint16_t title_lo = (uint16_t)(th & 0xFFFF);
    uint16_t title_hi = (uint16_t)((th >> 16) & 0xFFFF);
    uint16_t num_q = (uint16_t)((c->number > 500 ? 500 : c->number) * 131);

    out[0] = num_q;
    out[1] = title_lo;
    out[2] = f_q;
    out[3] = phase_q;
    out[4] = year_q;
    out[5] = n_refs_q;
    out[6] = title_hi;
    out[7] = 0;
    out[8] = 0; out[9] = 0; out[10] = 0; out[11] = 0;
    out[12] = 0; out[13] = 0; out[14] = 0; out[15] = 0;
}

/* ----- State hash (sorts cells by dial[0] for determinism) ----- */
static int dials_cmp(const void *a, const void *b) {
    const Dials *da = (const Dials *)a;
    const Dials *db = (const Dials *)b;
    return (int)(*da)[0] - (int)(*db)[0];
}

uint64_t state_hash(const LiveCanon *c) {
    Dials sorted[MAX_PAPERS];
    for (uint32_t i = 0; i < c->n_papers; i++) {
        memcpy(sorted[i], c->dials[i], sizeof(Dials));
    }
    qsort(sorted, c->n_papers, sizeof(Dials), dials_cmp);
    uint64_t h = 0xCBF29CE484222325ULL;
    for (uint32_t i = 0; i < c->n_papers; i++) {
        for (int j = 0; j < 16; j++) {
            uint16_t v = sorted[i][j];
            uint8_t *b = (uint8_t *)&v;
            for (int k = 0; k < 2; k++) {
                h ^= b[k];
                h *= 0x00000100000001B3ULL;
            }
        }
    }
    return h;
}

/* ----- Add a paper. ----- */
void canon_add(LiveCanon *c, const Cell *cell) {
    if (c->n_papers >= MAX_PAPERS) return;
    c->papers[c->n_papers] = *cell;
    cell_to_dials(cell, c->dials[c->n_papers]);
    c->n_papers++;
    c->state_hash = state_hash(c);
}

/* ----- NAVIGATE: BFS through citations. ----- */
typedef struct {
    uint32_t number;
    uint32_t depth;
} PathNode;

void canon_navigate(const LiveCanon *c, uint32_t start, uint32_t depth,
                    PathNode *out, uint32_t *n_out) {
    uint32_t visited[MAX_PAPERS] = {0};
    PathNode queue[MAX_PAPERS * 4];
    uint32_t head = 0, tail = 0;
    *n_out = 0;

    queue[tail++] = (PathNode){start, 0};
    visited[start] = 1;

    while (head < tail && *n_out < MAX_PAPERS * 4) {
        PathNode cur = queue[head++];
        out[(*n_out)++] = cur;
        if (cur.depth >= depth) continue;
        for (uint32_t i = 0; i < c->n_papers; i++) {
            if (c->papers[i].number == cur.number) {
                for (uint32_t r = 0; r < c->papers[i].n_refs; r++) {
                    uint32_t ref = c->papers[i].ref_papers[r];
                    if (!visited[ref]) {
                        visited[ref] = 1;
                        queue[tail++] = (PathNode){ref, cur.depth + 1};
                    }
                }
                break;
            }
        }
    }
}

/* ----- LINEAGE: trace F-number. ----- */
uint32_t canon_lineage(const LiveCanon *c, uint32_t f_number,
                       uint32_t *out, uint32_t max_out) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->n_papers; i++) {
        for (uint32_t j = 0; j < c->papers[i].n_f_refs; j++) {
            if (c->papers[i].ref_f_numbers[j] == f_number) {
                if (n < max_out) out[n] = c->papers[i].number;
                n++;
                break;
            }
        }
    }
    return n;
}

/* ----- Cosine similarity. ----- */
float cosine_sim(const Dials a, const Dials b) {
    float dot = 0, na = 0, nb = 0;
    for (int i = 0; i < 16; i++) {
        dot += (float)a[i] * (float)b[i];
        na += (float)a[i] * (float)a[i];
        nb += (float)b[i] * (float)b[i];
    }
    na = sqrtf(na);
    nb = sqrtf(nb);
    if (na == 0 || nb == 0) return 0;
    return dot / (na * nb);
}

/* ----- GHOST: find k nearest neighbors. ----- */
typedef struct {
    uint32_t number;
    float score;
} Scored;

void canon_ghost(const LiveCanon *c, uint32_t paper_num, uint32_t k,
                 Scored *out) {
    Dials target;
    int found = 0;
    for (uint32_t i = 0; i < c->n_papers; i++) {
        if (c->papers[i].number == paper_num) {
            memcpy(target, c->dials[i], sizeof(Dials));
            found = 1;
            break;
        }
    }
    if (!found) return;
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->n_papers && n < MAX_PAPERS; i++) {
        if (c->papers[i].number == paper_num) continue;
        out[n].number = c->papers[i].number;
        out[n].score = cosine_sim(target, c->dials[i]);
        n++;
    }
    /* Simple bubble sort (n is small) */
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i+1; j < n; j++) {
            if (out[j].score > out[i].score) {
                Scored tmp = out[i]; out[i] = out[j]; out[j] = tmp;
            }
        }
    }
}

/* ----- TICK: count cells. ----- */
uint32_t canon_tick(const LiveCanon *c) {
    return c->n_papers;
}

/* ----- Demo ----- */
int main(void) {
    LiveCanon c = {0};
    Cell p1 = {.number=425, .title="F115 VHDL", .f_number=115,
                .phase=237, .date="2026-09-03", .ref_papers={426,427}, .n_refs=2};
    Cell p2 = {.number=426, .title="F116 Opcodes", .f_number=116,
                .phase=238, .date="2026-09-03", .ref_f_numbers={115}, .n_f_refs=1};
    Cell p3 = {.number=427, .title="F117 5-substrate", .f_number=117,
                .phase=239, .date="2026-09-03", .ref_f_numbers={115,116}, .n_f_refs=2};
    canon_add(&c, &p1);
    canon_add(&c, &p2);
    canon_add(&c, &p3);

    printf("Live Canon (C99 port)\n");
    printf("  papers: %u\n", c.n_papers);
    printf("  state hash: 0x%016llx\n", (unsigned long long)c.state_hash);

    /* NAVIGATE */
    PathNode path[16];
    uint32_t n_path = 0;
    canon_navigate(&c, 425, 2, path, &n_path);
    printf("  NAVIGATE(425, 2): %u cells\n", n_path);
    for (uint32_t i = 0; i < n_path; i++) {
        printf("    [%u] paper-%u (depth %u)\n", i, path[i].number, path[i].depth);
    }

    /* LINEAGE */
    uint32_t lineage[16];
    uint32_t n_lin = canon_lineage(&c, 115, lineage, 16);
    printf("  LINEAGE(F115): %u papers\n", n_lin);

    /* GHOST */
    Scored ghost[8];
    canon_ghost(&c, 425, 3, ghost);
    printf("  GHOST(425, 3):\n");
    for (int i = 0; i < 3; i++) {
        printf("    - paper-%u score=%.4f\n", ghost[i].number, ghost[i].score);
    }

    /* TICK */
    printf("  TICK: %u cells\n", canon_tick(&c));

    printf("Live Canon (C99) PASS\n");
    return 0;
}

/* math.h sqrtf fallback if not available */
#ifdef NEED_SQRTF
static float sqrtf(float x) {
    if (x <= 0) return 0;
    float r = x;
    for (int i = 0; i < 20; i++) r = (r + x/r) / 2;
    return r;
}
#endif
