/* quilt/quf.h — the QUF cell kind (Phase 237 cutting-edge adoption #6).
 *
 * QUF = Quilt Universal Format. Borrowed and adapted from quilt-verilog's
 * GGUF-of-silicon state-serialization format (docs/QUF-SPEC.md, 2026-08-30).
 *
 * Polyformalism claim: a QUF file written by quilt-c is loadable by
 * quilt-verilog's RTL loader (rtl/q_uf_loader.v) on a real iCE40 FPGA, and
 * the reverse — a QUF file written by the Verilog fabric boots into a
 * quilt-c engine and produces the same dials, edges, and tick schedule.
 *
 *   - 16-byte fixed header: magic 'Q','U','F',0x00, version=1, endian=1,
 *     u32 kv_count
 *   - KV metadata (GGUF-style: name_len, name, value_type, value)
 *   - u32 section_count; per-section entry: name_len, name, kind, offset(u64),
 *     size(u64)
 *   - Section payloads at the named offsets, each aligned to a power of 2
 *
 * This C port implements a strict subset: dials + edges + ticks, no routing
 * yet (the host-side routing tables live in the engine; the silicon routes
 * by ring position, so a host-route section is interpretively lossy and
 * deferred). The header magic, KV walker, and section table conform to the
 * R1-R9 rules of QUF-SPEC.md §5a.
 *
 * The integration: a QUF cell in the C engine is an external kind whose
 * serialize() produces a byte buffer that any conformant reader (C, Python,
 * Verilog) can load; whose load() restores dials, edges, and tick schedule
 * from a byte buffer; and whose state_hash includes the QUF magic so a
 * tampered file fails PROOF verify_full() with a single bit-error.
 *
 * The cowboy's verdict: QUF is the smallest unit of "save state" for any
 * Quilt substrate. Same 5+1 opcodes everywhere. One file. Loads in sim,
 * in software, in silicon, identically.
 */
#ifndef QUILTQUF_H
#define QUILTQUF_H

#include <stdint.h>
#include <stddef.h>
#include "quilt/cell.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── On-wire constants (must match quilt-verilog rtl/q_uf_loader.v) ────── */
#define QUILT_QUF_MAGIC_0  'Q'
#define QUILT_QUF_MAGIC_1  'U'
#define QUILT_QUF_MAGIC_2  'F'
#define QUILT_QUF_MAGIC_3  0x00
#define QUILT_QUF_VERSION  1
#define QUILT_QUF_ENDIAN   1   /* little-endian only */
#define QUILT_QUF_ALIGN    32  /* default section alignment */

/* GGUF-style value types (subset; matches QUF-SPEC.md §4) */
typedef enum {
    QUILT_QUF_T_U8   = 0,
    QUILT_QUF_T_I8   = 1,
    QUILT_QUF_T_U16  = 2,
    QUILT_QUF_T_I16  = 3,
    QUILT_QUF_T_U32  = 4,
    QUILT_QUF_T_I32  = 5,
    QUILT_QUF_T_F32  = 6,
    QUILT_QUF_T_BOOL = 7,
    QUILT_QUF_T_STR  = 8,
    QUILT_QUF_T_ARR  = 9,
    QUILT_QUF_T_U64  = 10,
    QUILT_QUF_T_I64  = 11,
    QUILT_QUF_T_F64  = 12,
} quilt_quf_valtype_t;

/* Section kinds (only kind=0 is standard; others are skipped by readers
 * that don't understand them — the extensibility rule). */
#define QUILT_QUF_KIND_RAW 0

/* Standard section names (the v1 set this C port writes) */
#define QUILT_QUF_SEC_DIALS  "dials"
#define QUILT_QUF_SEC_EDGES  "edges"
#define QUILT_QUF_SEC_TICKS  "ticks"
#define QUILT_QUF_SEC_PROOF  "proof"   /* optional: include PROOF chain */

/* Per-cell dial row: 32 bytes matches §6's `dials: cell_count×32`. The
 * QUF-SPEC reserves 32 bytes/dial; the layout in this C port is the
 * lower 16 bits = int_value, next 16 bits = float Q15.15, then a 4-bit
 * type tag + 4 reserved bits, then 12 bytes zero padding. The exact
 * layout is the polyformalism contract; the higher 12 bytes are host-
 * specific overlay. */
typedef struct {
    uint16_t i16;     /* integer value (16-bit) */
    uint16_t q1515;   /* fixed-point Q15.15 (host overlay of float) */
    uint8_t  tag;     /* quilt_vtype_t tag */
    uint8_t  rsvd[3]; /* reserved; must be 0 */
    uint8_t  pad[24]; /* future / host overlay */
} quilt_quf_dial_row_t;

#define QUILT_QUF_DIAL_ROW_BYTES 32   /* MUST equal sizeof(quilt_quf_dial_row_t) */

/* Per-edge record (v1: 12 + K bytes; this C port uses K=8 ladder buckets,
 * matching the Verilog default edge.k=8). */
#define QUILT_QUF_EDGE_K_DEFAULT 8
typedef struct {
    uint16_t src;        /* source cell id */
    uint16_t dst;        /* dest cell id */
    uint16_t base_w;     /* base weight (Q1.15 signed: bits[15]=sign, [14:0]=frac) */
    uint16_t flags;      /* bit 0: valid; bits 1..15: host overlay */
    uint32_t walk_count; /* # of times this edge fired (Hebbian walk count) */
    uint16_t ladder[QUILT_QUF_EDGE_K_DEFAULT]; /* 8 ladder buckets, each u16 */
} quilt_quf_edge_row_t;

/* Tick row: 4 bytes per cell (u32 tick period). */
typedef struct {
    uint32_t period;
} quilt_quf_tick_row_t;

/* ── Engine: a QUF cell is state plus the 3 standard sections ────────── */
typedef struct {
    /* Cell identity (a QUF file is a multi-cell snapshot, but a QUF cell
     * in the C engine is a single snapshot) */
    uint16_t cell_count;
    uint16_t edge_count;
    uint16_t route_count;
    uint8_t  edge_k;          /* ladder buckets per edge; default 8 */

    /* Section payloads (caller-owned; sizes below) */
    quilt_quf_dial_row_t *dials;     /* cell_count entries */
    quilt_quf_edge_row_t *edges;     /* edge_count entries */
    uint32_t             *ticks;     /* cell_count entries (4 bytes each) */
    const uint8_t        *proof;     /* optional PROOF chain; 0 = none */
    size_t                proof_len;

    /* Writer scratch */
    uint8_t  *buf;     /* the assembled QUF file */
    size_t    buf_cap; /* capacity of buf (caller-allocated) */
    size_t    buf_len; /* current fill (after serialize) */
} quilt_quf_t;

/* ── Lifecycle ───────────────────────────────────────────────────────── */
int  quilt_quf_init(quilt_quf_t *q,
                    uint16_t cell_count, uint16_t edge_count,
                    uint16_t route_count, uint8_t edge_k);
void quilt_quf_free(quilt_quf_t *q);

/* Attach caller-owned payloads. All three are required; ticks may be NULL
 * (defaults to 0 = periodic every tick). proof is optional (NULL = omit
 * the proof section, which keeps the file PROOF-chain-agnostic). */
int  quilt_quf_attach(quilt_quf_t *q,
                      quilt_quf_dial_row_t *dials,
                      quilt_quf_edge_row_t *edges,
                      uint32_t             *ticks,
                      const uint8_t        *proof, size_t proof_len);

/* ── Serialize: write a QUF file into q->buf. ────────────────────────── */
/* Returns 0 on success, -1 if buf is too small (required size is
 * q->buf_len after the call). The caller is responsible for sizing buf
 * (use quilt_quf_sizeof() to ask first). */
int  quilt_quf_serialize(quilt_quf_t *q);

/* ── Deserialize: parse a QUF file. ─────────────────────────────────── */
/* Verifies R1 (magic), R2 (endian=1), R3 (truncation), R5 (4 GiB ceiling),
 * R6 (no payload overlap with front matter), R7 (size formulas), R9
 * (alignment), R11 (zero padding). Returns 0 on success, -1 on any rule
 * violation. After return, q->dials / q->edges / q->ticks / q->proof
 * point INTO the input buffer (zero-copy; the buffer must outlive q). */
int  quilt_quf_deserialize(quilt_quf_t *q, const uint8_t *buf, size_t len);

/* ── Section table: the caller can read parsed section info ─────────── */
typedef struct {
    char     name[32];
    uint32_t kind;
    uint64_t offset;
    uint64_t size;
} quilt_quf_section_info_t;

/* Walk the section table; returns the i-th entry (0-based). On i out of
 * range, returns NULL. The q->dials/edges/ticks/proof pointers above are
 * the convenience view; this API gives the raw table. */
const quilt_quf_section_info_t *quilt_quf_section(const quilt_quf_t *q, size_t i);
size_t quilt_quf_section_count(const quilt_quf_t *q);

/* ── Size query: minimum buf size to hold a serialize() of this q ───── */
size_t quilt_quf_sizeof(const quilt_quf_t *q);

/* ── Dial accessors: bridge quilt_value_t <-> QUF dial rows ─────────── */
void  quilt_quf_dial_from_value(quilt_quf_dial_row_t *row, const quilt_value_t *v);
void  quilt_quf_dial_to_value(const quilt_quf_dial_row_t *row, quilt_value_t *v);

/* ── State hash: FNV-1a 64-bit of the QUF file (for PROOF integration) ─ */
/* Used by quilt_proof_append() callers who want the state_hash to include
 * the serialized file. The hash is identical to a verifier walking the
 * PROOF chain and hashing the .quf bytes that produced the chain. */
uint64_t quilt_quf_hash(const uint8_t *buf, size_t len);

/* ── Opcode: BIND a cell's dials from a QUF section ─────────────────── */
/* A QUF cell is an external kind. The op handler walks the named section
 * and copies rows into the engine's cell array. Returns 0 on success,
 * -1 if the section is missing or the row count mismatches cell_count. */
int  quilt_quf_op_bind(quilt_quf_t *q, const char *section_name,
                       quilt_cell_t *cells, size_t n_cells);

/* ── EILEEN-chain seal: PROOF-append + QUF write in one call ─────────── */
/* Builds the PROOF entry (state_hash = FNV-1a(q.buf[0..q.buf_len])) and
 * writes a new QUF file that includes the proof section. The original
 * q is unchanged; a new QUF is built in q->buf (must be sized for the
 * enlarged file). Returns 0 on success, -1 on size/parse error. */
int  quilt_quf_seal(quilt_quf_t *q);

#ifdef __cplusplus
}
#endif

#endif /* QUILTQUF_H */
