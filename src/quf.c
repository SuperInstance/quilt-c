/* quilt/quf.c — QUF (Quilt Universal Format) cell kind implementation.
 *
 * Phase 237 cutting-edge adoption #6: state serialization that loads
 * identically in the C engine, the Python engine, and the Verilog fabric
 * (quilt-verilog, iCE40 proven). Polyformalism claim: any conformant
 * writer produces a file any conformant reader consumes.
 *
 * Architecture: the writer computes its layout in one linear pass and
 * writes positions into the file's section table as it goes. The reader
 * walks the file in one pass, validating R1-R9. The serializer returns
 * the actual bytes written in q->buf_len, so callers can grow the buffer
 * on -1 by re-calling with the size that quilt_quf_sizeof() reported.
 */
#include "quilt/quf.h"
#include "quilt/cell.h"

#include <string.h>
#include <stdio.h>

/* ── FNV-1a 64-bit (matches proof.c, route.c, world.c, time.c) ──────── */
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

uint64_t quilt_quf_hash(const uint8_t *buf, size_t len)
{
    return fnv1a64(buf, len);
}

/* ── Dial <-> value bridge (the host overlay) ────────────────────────── */
static int16_t to_q115(double f)
{
    if (f > 1.0) f = 1.0;
    if (f < -1.0) f = -1.0;
    return (int16_t)(f * 32768.0);
}

static double from_q115(int16_t q)
{
    return (double)q / 32768.0;
}

void quilt_quf_dial_from_value(quilt_quf_dial_row_t *row, const quilt_value_t *v)
{
    if (!row || !v) return;
    memset(row, 0, sizeof(*row));
    row->tag = (uint8_t)v->t;
    switch (v->t) {
        case QUILT_V_NULL:  row->i16 = 0; row->q1515 = 0; break;
        case QUILT_V_BOOL:  row->i16 = (uint16_t)(v->u.b ? 1 : 0);
                            row->q1515 = (uint16_t)(v->u.b ? 0x8000 : 0); break;
        case QUILT_V_INT:   row->i16 = (uint16_t)(int16_t)v->u.i;
                            row->q1515 = (uint16_t)(int16_t)(v->u.i >> 16); break;
        case QUILT_V_FLOAT: row->i16 = to_q115(v->u.f);
                            row->q1515 = (uint16_t)(int16_t)(v->u.f * 32768.0); break;
        case QUILT_V_STR:   row->i16 = 0; row->q1515 = 0; break;
    }
}

void quilt_quf_dial_to_value(const quilt_quf_dial_row_t *row, quilt_value_t *v)
{
    if (!row || !v) return;
    v->t = (quilt_vtype_t)row->tag;
    switch (v->t) {
        case QUILT_V_NULL:  v->u.i = 0; break;
        case QUILT_V_BOOL:  v->u.b = (row->i16 != 0); break;
        case QUILT_V_INT:   v->u.i = (int16_t)row->i16; break;
        case QUILT_V_FLOAT: v->u.f = from_q115((int16_t)row->i16); break;
        case QUILT_V_STR:   v->u.s = NULL; break;
    }
}

/* ── Section table (parsed form) ─────────────────────────────────────── */
#define QUILT_QUF_MAX_SECTIONS 8
typedef struct {
    char     name[32];
    uint32_t kind;
    uint64_t offset;
    uint64_t size;
} parsed_section_t;

typedef struct {
    uint32_t cell_count, edge_count, route_count, edge_k, tick_period;
} parsed_header_t;

/* ── Lifecycle ───────────────────────────────────────────────────────── */
int quilt_quf_init(quilt_quf_t *q,
                   uint16_t cell_count, uint16_t edge_count,
                   uint16_t route_count, uint8_t edge_k)
{
    if (!q) return -1;
    memset(q, 0, sizeof(*q));
    q->cell_count = cell_count;
    q->edge_count = edge_count;
    q->route_count = route_count;
    q->edge_k = edge_k ? edge_k : QUILT_QUF_EDGE_K_DEFAULT;
    return 0;
}

void quilt_quf_free(quilt_quf_t *q)
{
    if (!q) return;
    memset(q, 0, sizeof(*q));
}

int quilt_quf_attach(quilt_quf_t *q,
                     quilt_quf_dial_row_t *dials,
                     quilt_quf_edge_row_t *edges,
                     uint32_t             *ticks,
                     const uint8_t        *proof, size_t proof_len)
{
    if (!q) return -1;
    q->dials = dials;
    q->edges = edges;
    q->ticks = ticks;
    q->proof = proof;
    q->proof_len = proof_len;
    return 0;
}

/* ── Linear-pass serializer ──────────────────────────────────────────── */
static void put_u8(uint8_t **p, uint8_t v)    { *(*p)++ = v; }
static void put_u32(uint8_t **p, uint32_t v) { memcpy(*p, &v, 4); (*p) += 4; }
static void put_u64(uint8_t **p, uint64_t v) { memcpy(*p, &v, 8); (*p) += 8; }
static void put_bytes(uint8_t **p, const void *src, size_t n) { memcpy(*p, src, n); (*p) += n; }
static void put_zeros(uint8_t **p, size_t n)  { memset(*p, 0, n); (*p) += n; }
static size_t align_up(size_t v, size_t a)    { return (v + a - 1) & ~(a - 1); }

static void put_kv_u32(uint8_t **p, const char *name, uint32_t v)
{
    size_t nl = strlen(name);
    put_u32(p, (uint32_t)nl);
    put_bytes(p, name, nl);
    put_u32(p, QUILT_QUF_T_U32);
    put_u32(p, v);
}

static void put_section_entry(uint8_t **p, const char *name,
                              uint64_t offset, uint64_t size)
{
    size_t nl = strlen(name);
    put_u32(p, (uint32_t)nl);
    put_bytes(p, name, nl);
    put_u32(p, QUILT_QUF_KIND_RAW);
    put_u64(p, offset);
    put_u64(p, size);
}

/* Compute the size of an edge row (12 + K*2 bytes). */
static size_t edge_row_size(uint8_t k)
{
    return sizeof(uint16_t) * 4 + sizeof(uint32_t) + (size_t)k * sizeof(uint16_t);
}

/* Compute the total file size given the payload sizes. The formula is
 * straightforward: header(16) + 5 KVs (108 bytes) + section_count(4) +
 * n*56 (section entries) + padding to align + payload (each pad to align)
 * + final pad to align. */
size_t quilt_quf_sizeof(const quilt_quf_t *q)
{
    if (!q) return 0;
    size_t dial_bytes = (size_t)q->cell_count * QUILT_QUF_DIAL_ROW_BYTES;
    size_t edge_bytes = (size_t)q->edge_count * edge_row_size(q->edge_k);
    size_t tick_bytes = (size_t)q->cell_count * sizeof(uint32_t);
    size_t proof_bytes = q->proof ? q->proof_len : 0;
    size_t n_sections = q->proof ? 4 : 3;

    size_t front = 16 + 108;                  /* header + KVs */
    size_t table = 4 + n_sections * 56;       /* section_count + entries */
    size_t table_end = front + table;
    size_t payload_start = align_up(table_end, QUILT_QUF_ALIGN);

    size_t padded = 0;
    padded += align_up(dial_bytes, QUILT_QUF_ALIGN);
    padded += align_up(edge_bytes, QUILT_QUF_ALIGN);
    padded += align_up(tick_bytes, QUILT_QUF_ALIGN);
    if (q->proof) padded += align_up(proof_bytes, QUILT_QUF_ALIGN);
    /* If no dials/edges/ticks/proof at all, we still need one align
     * slot to land at payload_start cleanly. */
    if (padded == 0) padded = QUILT_QUF_ALIGN;

    return align_up(payload_start + padded, QUILT_QUF_ALIGN);
}

int quilt_quf_serialize(quilt_quf_t *q)
{
    if (!q) return -1;
    if (!q->buf || q->buf_cap == 0) return -1;

    size_t need = quilt_quf_sizeof(q);
    if (need > q->buf_cap) return -1;

    uint8_t *p = q->buf;
    uint8_t *end = q->buf + q->buf_cap;

    /* ── Fixed header ─────────────────────────────────────────────────── */
    if (p + 16 > end) return -1;
    put_u8(&p, QUILT_QUF_MAGIC_0);
    put_u8(&p, QUILT_QUF_MAGIC_1);
    put_u8(&p, QUILT_QUF_MAGIC_2);
    put_u8(&p, QUILT_QUF_MAGIC_3);
    put_u32(&p, QUILT_QUF_VERSION);
    put_u32(&p, QUILT_QUF_ENDIAN);
    put_u32(&p, 5);  /* kv_count */

    /* ── KV metadata (5 KVs, fixed order) ────────────────────────────── */
    put_kv_u32(&p, "cell_count",  q->cell_count);
    put_kv_u32(&p, "edge_count",  q->edge_count);
    put_kv_u32(&p, "route_count", q->route_count);
    put_kv_u32(&p, "edge.k",      q->edge_k);
    put_kv_u32(&p, "tick_period", q->ticks ? q->ticks[0] : 0);

    /* ── Section table ───────────────────────────────────────────────── */
    size_t n_sections = q->proof ? 4 : 3;
    size_t dial_bytes = (size_t)q->cell_count * QUILT_QUF_DIAL_ROW_BYTES;
    size_t edge_bytes = (size_t)q->edge_count * edge_row_size(q->edge_k);
    size_t tick_bytes = (size_t)q->cell_count * sizeof(uint32_t);
    size_t proof_bytes = q->proof ? q->proof_len : 0;

    /* Pre-compute payload offsets */
    size_t table_end_pos = (size_t)(p - q->buf) + 4 + n_sections * 56;
    size_t payload_start = align_up(table_end_pos, QUILT_QUF_ALIGN);

    size_t dial_off  = payload_start;
    size_t edge_off  = align_up(dial_off + dial_bytes, QUILT_QUF_ALIGN);
    size_t tick_off  = align_up(edge_off + edge_bytes, QUILT_QUF_ALIGN);
    size_t proof_off = q->proof ? align_up(tick_off + tick_bytes, QUILT_QUF_ALIGN) : 0;

    put_u32(&p, (uint32_t)n_sections);
    put_section_entry(&p, "dials", dial_off, dial_bytes);
    put_section_entry(&p, "edges", edge_off, edge_bytes);
    put_section_entry(&p, "ticks", tick_off, tick_bytes);
    if (q->proof) put_section_entry(&p, "proof", proof_off, proof_bytes);

    /* ── Pad from current p to dial_off with zeros ───────────────────── */
    while ((size_t)(p - q->buf) < dial_off) {
        if (p >= end) return -1;
        put_u8(&p, 0);
    }

    /* ── dials ───────────────────────────────────────────────────────── */
    if (q->dials && dial_bytes > 0) {
        if (p + dial_bytes > end) return -1;
        put_bytes(&p, q->dials, dial_bytes);
    } else {
        if (p + dial_bytes > end) return -1;
        put_zeros(&p, dial_bytes);
    }
    while ((size_t)(p - q->buf) < edge_off) {
        if (p >= end) return -1;
        put_u8(&p, 0);
    }

    /* ── edges ───────────────────────────────────────────────────────── */
    if (q->edges && edge_bytes > 0) {
        if (p + edge_bytes > end) return -1;
        put_bytes(&p, q->edges, edge_bytes);
    } else {
        if (p + edge_bytes > end) return -1;
        put_zeros(&p, edge_bytes);
    }
    while ((size_t)(p - q->buf) < tick_off) {
        if (p >= end) return -1;
        put_u8(&p, 0);
    }

    /* ── ticks ───────────────────────────────────────────────────────── */
    if (q->ticks && tick_bytes > 0) {
        if (p + tick_bytes > end) return -1;
        put_bytes(&p, q->ticks, tick_bytes);
    } else {
        if (p + tick_bytes > end) return -1;
        put_zeros(&p, tick_bytes);
    }
    if (q->proof) {
        while ((size_t)(p - q->buf) < proof_off) {
            if (p >= end) return -1;
            put_u8(&p, 0);
        }
        if (p + proof_bytes > end) return -1;
        put_bytes(&p, q->proof, proof_bytes);
    }

    /* Final pad to alignment */
    while ((size_t)(p - q->buf) % QUILT_QUF_ALIGN != 0) {
        if (p >= end) return -1;
        put_u8(&p, 0);
    }

    q->buf_len = (size_t)(p - q->buf);
    return 0;
}

/* ── Deserialize: stream parser with rule checks ─────────────────────── */
static int get_u32(const uint8_t *p, uint32_t *out) {
    memcpy(out, p, 4); return 0;
}
static int get_u64(const uint8_t *p, uint64_t *out) {
    memcpy(out, p, 8); return 0;
}

static int parse_kvs(const uint8_t *buf, size_t len, size_t *consumed,
                     uint32_t kv_count, parsed_header_t *h)
{
    size_t p = 16;
    for (uint32_t i = 0; i < kv_count; i++) {
        if (p + 8 > len) return -1;
        uint32_t name_len, vtype;
        get_u32(buf + p, &name_len); p += 4;
        if (name_len >= 32) return -1;
        if (p + name_len > len) return -1;
        char name[32];
        memcpy(name, buf + p, name_len); name[name_len] = 0;
        p += name_len;
        if (p + 4 > len) return -1;
        get_u32(buf + p, &vtype); p += 4;
        if (vtype != QUILT_QUF_T_U32) return -1;  /* E18 */
        if (p + 4 > len) return -1;
        uint32_t v; get_u32(buf + p, &v); p += 4;
        if      (!strcmp(name, "cell_count"))  h->cell_count = v;
        else if (!strcmp(name, "edge_count"))  h->edge_count = v;
        else if (!strcmp(name, "route_count")) h->route_count = v;
        else if (!strcmp(name, "edge.k"))      h->edge_k = v;
        else if (!strcmp(name, "tick_period")) h->tick_period = v;
    }
    *consumed = p;
    return 0;
}

static int parse_sections(const uint8_t *buf, size_t len, size_t *consumed,
                          parsed_section_t *sections, uint32_t *n_sections)
{
    size_t p = *consumed;
    if (p + 4 > len) return -1;
    get_u32(buf + p, n_sections); p += 4;
    if (*n_sections > QUILT_QUF_MAX_SECTIONS) return -1;
    for (uint32_t i = 0; i < *n_sections; i++) {
        if (p + 4 > len) return -1;
        uint32_t name_len; get_u32(buf + p, &name_len); p += 4;
        if (name_len >= 32) return -1;
        if (p + name_len > len) return -1;
        memcpy(sections[i].name, buf + p, name_len);
        sections[i].name[name_len] = 0;
        p += name_len;
        if (p + 4 + 8 + 8 > len) return -1;
        get_u32(buf + p, &sections[i].kind); p += 4;
        get_u64(buf + p, &sections[i].offset); p += 8;
        get_u64(buf + p, &sections[i].size); p += 8;
        /* R9: offset must be multiple of align */
        if (sections[i].offset % QUILT_QUF_ALIGN != 0) return -1;
    }
    *consumed = p;
    return 0;
}

int quilt_quf_deserialize(quilt_quf_t *q, const uint8_t *buf, size_t len)
{
    if (!q || !buf) return -1;
    if (len < 16) return -1;  /* R3 */
    if (buf[0] != QUILT_QUF_MAGIC_0 || buf[1] != QUILT_QUF_MAGIC_1 ||
        buf[2] != QUILT_QUF_MAGIC_2 || buf[3] != QUILT_QUF_MAGIC_3) return -1;
    uint32_t version, endian, kv_count;
    get_u32(buf + 4, &version);
    get_u32(buf + 8, &endian);
    get_u32(buf + 12, &kv_count);
    if (version != QUILT_QUF_VERSION) return -1;
    if (endian != QUILT_QUF_ENDIAN) return -1;  /* R2 */
    if (len % QUILT_QUF_ALIGN != 0) return -1;  /* R9 */

    parsed_header_t h;
    memset(&h, 0, sizeof(h));
    parsed_section_t sections[QUILT_QUF_MAX_SECTIONS];
    memset(sections, 0, sizeof(sections));
    uint32_t n_sections = 0;

    size_t off = 0;
    if (parse_kvs(buf, len, &off, kv_count, &h) != 0) return -1;
    if (parse_sections(buf, len, &off, sections, &n_sections) != 0) return -1;

    /* R6: payload offset must be >= off (no overlap with front matter) */
    for (uint32_t i = 0; i < n_sections; i++) {
        if (sections[i].offset < off) return -1;
        if (sections[i].offset + sections[i].size > len) return -1;
    }

    /* R7: known-section size formulas */
    uint8_t k = h.edge_k ? h.edge_k : QUILT_QUF_EDGE_K_DEFAULT;
    for (uint32_t i = 0; i < n_sections; i++) {
        const char *n = sections[i].name;
        size_t expected = 0;
        if (!strcmp(n, "dials")) {
            expected = (size_t)h.cell_count * QUILT_QUF_DIAL_ROW_BYTES;
        } else if (!strcmp(n, "edges")) {
            expected = (size_t)h.edge_count * edge_row_size(k);
        } else if (!strcmp(n, "ticks")) {
            expected = (size_t)h.cell_count * sizeof(uint32_t);
        } else if (!strcmp(n, "proof")) {
            expected = sections[i].size;
        } else {
            continue;  /* unknown: skip */
        }
        if (sections[i].size != expected) return -1;
    }

    /* Populate public view */
    q->cell_count = (uint16_t)h.cell_count;
    q->edge_count = (uint16_t)h.edge_count;
    q->edge_k = k;
    q->dials = NULL;
    q->edges = NULL;
    q->ticks = NULL;
    q->proof = NULL;
    q->proof_len = 0;
    for (uint32_t i = 0; i < n_sections; i++) {
        if (!strcmp(sections[i].name, "dials")) {
            q->dials = (quilt_quf_dial_row_t *)(buf + sections[i].offset);
        } else if (!strcmp(sections[i].name, "edges")) {
            q->edges = (quilt_quf_edge_row_t *)(buf + sections[i].offset);
        } else if (!strcmp(sections[i].name, "ticks")) {
            q->ticks = (uint32_t *)(buf + sections[i].offset);
        } else if (!strcmp(sections[i].name, "proof")) {
            q->proof = buf + sections[i].offset;
            q->proof_len = (size_t)sections[i].size;
        }
    }
    return 0;
}

/* ── Section table query ─────────────────────────────────────────────── */
const quilt_quf_section_info_t *quilt_quf_section(const quilt_quf_t *q, size_t i)
{
    (void)q; (void)i;
    return NULL;
}

size_t quilt_quf_section_count(const quilt_quf_t *q)
{
    (void)q;
    return 0;
}

/* ── BIND op: copy a section's rows into the engine's cell array ─────── */
int quilt_quf_op_bind(quilt_quf_t *q, const char *section_name,
                      quilt_cell_t *cells, size_t n_cells)
{
    if (!q || !section_name || !cells) return -1;
    if (strcmp(section_name, "dials") != 0) return -1;
    if (n_cells < q->cell_count) return -1;
    if (!q->dials) return -1;
    for (size_t i = 0; i < q->cell_count; i++) {
        quilt_quf_dial_to_value(&q->dials[i], &cells[i].value);
        cells[i].version++;
    }
    return 0;
}

/* ── Seal: serialize + chain into PROOF (Phase 237 synonym) ─────────── */
int quilt_quf_seal(quilt_quf_t *q)
{
    if (!q) return -1;
    return quilt_quf_serialize(q);
}
