/* quilt/proof.c — the PROOF opcode runtime.
 *
 * In this C port we use a tiny built-in FNV-1a hash for the
 * `state_hash` field (kernel-friendly, no stdlib, no external deps).
 * The signature field is left zeroed; on a real substrate the
 * polyformalism binding fills it via ed25519. The polyformalism
 * claim is the ring shape, not the crypto choice.
 *
 * The point: PROOF cells live on every Quilt substrate. The opcode
 * is the same; the hash + sign implementations differ.
 */
#include "quilt/proof.h"

#include <string.h>

/* FNV-1a 64-bit; cheap, deterministic, no external dep */
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

static void hash_state(const quilt_value_t *v, uint8_t out[32])
{
    /* The 32-byte digest is FNV-1a over the *active* cell value:
     *   - the type tag (4 bytes)
     *   - the active value (8 bytes for int/bool/float; the string
     *     bytes for str; nothing for null)
     *
     * Critically: we do NOT hash the raw struct (the union has 24
     * bytes of which only 8 are active for scalars, and the string
     * pointer is borrowed — we hash the pointed-to bytes, not the
     * pointer value, so two cells with the same string content hash
     * the same). */
    memset(out, 0, 32);
    if (!v) return;
    /* Mix the type tag into the FNV state */
    uint64_t h = 0xcbf29ce484222325ULL;
    h ^= (uint8_t)v->t;
    h *= 0x100000001b3ULL;
    h ^= (uint8_t)((int)v->t >> 8);
    h *= 0x100000001b3ULL;
    /* Mix the active value */
    switch (v->t) {
        case QUILT_V_NULL:
            /* nothing to mix */
            break;
        case QUILT_V_BOOL:
            h ^= (uint8_t)(v->u.b ? 1 : 0);
            h *= 0x100000001b3ULL;
            break;
        case QUILT_V_INT:
            for (int i = 0; i < 8; i++) {
                h ^= (uint8_t)((uint64_t)v->u.i >> (i * 8));
                h *= 0x100000001b3ULL;
            }
            break;
        case QUILT_V_FLOAT: {
            /* Hash the IEEE 754 bits, not the float value (NaN
             * canonicalization is messy) */
            uint64_t bits;
            memcpy(&bits, &v->u.f, 8);
            for (int i = 0; i < 8; i++) {
                h ^= (uint8_t)(bits >> (i * 8));
                h *= 0x100000001b3ULL;
            }
            break;
        }
        case QUILT_V_STR:
            if (v->u.s) {
                for (const char *p = v->u.s; *p; p++) {
                    h ^= (uint8_t)*p;
                    h *= 0x100000001b3ULL;
                }
            }
            break;
        default:
            break;
    }
    /* Expand the 64-bit FNV-1a to 32 bytes by writing h in 4 variants */
    for (int i = 0; i < 4; i++) {
        uint64_t slice = h + (uint64_t)i * 0x9e3779b97f4a7c15ULL;
        memcpy(&out[i * 8], &slice, 8);
    }
}

int quilt_proof_init(quilt_proof_t *p, quilt_proof_entry_t *ring)
{
    if (!p || !ring) return -1;
    p->ring = ring;
    p->head = 0;
    p->count = 0;
    memset(p->pub, 0, 32);
    memset(p->sec, 0, 32);
    p->nonce = 0;
    return 0;
}

int quilt_proof_set_secret(quilt_proof_t *p, const uint8_t sec[32])
{
    if (!p || !sec) return -1;
    memcpy(p->sec, sec, 32);
    return 0;
}

void quilt_proof_free(quilt_proof_t *p)
{
    if (!p) return;
    p->ring = NULL;
    p->head = 0;
    p->count = 0;
    /* Don't zero sec — the caller owns it and may have other copies. */
}

/* HMAC-like construction over FNV-1a: sig = expand64(sec XOR opad ||
 * expand64(inner_hash(message))). The polyformalism claim is the
 * shape (signing prev_hash || state_hash || tick || version with a
 * secret). On real substrates this is ed25519_sign(sec, message). */
static uint64_t sec_nonzero(const uint8_t sec[32])
{
    for (int i = 0; i < 32; i++) if (sec[i]) return 1;
    return 0;
}

static uint64_t fnv1a64_update(uint64_t h, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static void compute_sig(const uint8_t sec[32],
                         const uint8_t prev_hash[32],
                         const uint8_t state_hash[32],
                         uint64_t tick, uint64_t version,
                         uint64_t nonce, uint8_t sig_out[64])
{
    /* If sec is all-zero (test mode), leave sig zeroed. */
    if (!sec_nonzero(sec)) {
        memset(sig_out, 0, 64);
        return;
    }
    /* Inner hash: FNV-1a over (sec XOR ipad) || prev_hash || state_hash || tick || version || nonce */
    uint8_t k_ipad[32];
    for (int i = 0; i < 32; i++) k_ipad[i] = sec[i] ^ 0x36;
    uint64_t h = 0xcbf29ce484222325ULL;
    h = fnv1a64_update(h, k_ipad, 32);
    h = fnv1a64_update(h, prev_hash, 32);
    h = fnv1a64_update(h, state_hash, 32);
    h = fnv1a64_update(h, &tick, sizeof(tick));
    h = fnv1a64_update(h, &version, sizeof(version));
    h = fnv1a64_update(h, &nonce, sizeof(nonce));
    /* Outer hash: FNV-1a over (sec XOR opad) || expand(inner) */
    uint8_t k_opad[32];
    for (int i = 0; i < 32; i++) k_opad[i] = sec[i] ^ 0x5c;
    uint64_t h2 = 0xcbf29ce484222325ULL;
    h2 = fnv1a64_update(h2, k_opad, 32);
    uint8_t inner_bytes[8];
    memcpy(inner_bytes, &h, 8);
    h2 = fnv1a64_update(h2, inner_bytes, 8);
    /* Expand the 64-bit h2 to 64 bytes for the sig field */
    for (int i = 0; i < 8; i++) {
        uint64_t slice = h2 + (uint64_t)i * 0x9e3779b97f4a7c15ULL;
        memcpy(&sig_out[i * 8], &slice, 8);
    }
}

static int compute_prev_hash(const quilt_proof_t *p, uint8_t out[32])
{
    if (p->count == 0) {
        memset(out, 0, 32);
        return 0;
    }
    size_t prev = (p->head + QUILT_PROOF_RING_CAP - 1) % QUILT_PROOF_RING_CAP;
    memcpy(out, p->ring[prev].state_hash, 32);
    return 0;
}

int quilt_proof_append(quilt_proof_t *p, const quilt_value_t *state,
                        uint64_t tick, uint64_t version)
{
    if (!p || !p->ring || !state) return -1;
    /* prev_hash: the *chronologically* previous entry's state_hash. */
    compute_prev_hash(p, p->ring[p->head].prev_hash);
    /* state_hash: fingerprint of the current cell state */
    hash_state(state, p->ring[p->head].state_hash);
    /* sig: HMAC-style over the chain link */
    p->nonce++;
    compute_sig(p->sec,
                 p->ring[p->head].prev_hash,
                 p->ring[p->head].state_hash,
                 tick, version, p->nonce,
                 p->ring[p->head].sig);
    p->ring[p->head].tick = tick;
    p->ring[p->head].version = version;
    p->head = (p->head + 1) % QUILT_PROOF_RING_CAP;
    if (p->count < QUILT_PROOF_RING_CAP) p->count++;
    return 0;
}

int quilt_proof_verify(const quilt_proof_t *p)
{
    if (!p || !p->ring) return 0;
    if (p->count == 0) return 1;  /* empty ring is trivially valid */
    /* The oldest entry currently in the ring sits at position
     *   start = (head + CAP - count) mod CAP
     * (when count == CAP, start == head). The chain is the
     * contiguous arc start, start+1, ..., start+count-1 (mod CAP).
     *
     * Each entry's prev_hash must equal the previous entry's
     * state_hash. The oldest entry's prev_hash is undefined
     * (its predecessor has been overwritten), so we skip that
     * check and seed `expected` with its state_hash. */
    size_t start = (p->head + QUILT_PROOF_RING_CAP - p->count) % QUILT_PROOF_RING_CAP;
    uint8_t expected[32];
    memcpy(expected, p->ring[start].state_hash, 32);
    /* Walk start+1 through start+count (chronological successor of
     * each entry). The very first check is on entry start+1, whose
     * prev_hash must equal entry start's state_hash. */
    for (size_t k = 1; k < p->count; k++) {
        size_t i = (start + k) % QUILT_PROOF_RING_CAP;
        if (memcmp(p->ring[i].prev_hash, expected, 32) != 0) return 0;
        memcpy(expected, p->ring[i].state_hash, 32);
    }
    return 1;
}

int quilt_proof_verify_full(const quilt_proof_t *p)
{
    if (!p || !p->ring) return 0;
    if (p->count == 0) return 1;
    /* First check the prev_hash links. */
    if (!quilt_proof_verify(p)) return 0;
    /* If sec is all-zero, sigs are zeroed and we skip sig checks. */
    if (!sec_nonzero(p->sec)) {
        /* Just check that every sig is zeroed (test mode). */
        for (size_t k = 0; k < p->count; k++) {
            size_t i = ((p->head + QUILT_PROOF_RING_CAP - p->count + k)
                          % QUILT_PROOF_RING_CAP);
            for (int b = 0; b < 64; b++) {
                if (p->ring[i].sig[b] != 0) return 0;
            }
        }
        return 1;
    }
    /* Recompute every sig and compare. The nonce sequence is
     * 1, 2, 3, ... in append order. After wraparound we can't
     * recompute the absolute nonce for each entry from the data
     * alone, so we walk in append order from oldest to newest,
     * counting nonces. */
    size_t start = (p->head + QUILT_PROOF_RING_CAP - p->count) % QUILT_PROOF_RING_CAP;
    uint64_t nonce = p->count - p->count;  /* placeholder */
    /* The first entry's nonce is (total_appends_so_far - count + 1).
     * We don't know total_appends, but we can derive: the oldest
     * entry's nonce = p->nonce - count + 1. */
    if (p->count > p->nonce) return 0;  /* sanity: nonce must be >= count */
    nonce = p->nonce - p->count + 1;
    uint8_t expected_sig[64];
    for (size_t k = 0; k < p->count; k++) {
        size_t i = (start + k) % QUILT_PROOF_RING_CAP;
        compute_sig(p->sec, p->ring[i].prev_hash, p->ring[i].state_hash,
                     p->ring[i].tick, p->ring[i].version, nonce + k,
                     expected_sig);
        if (memcmp(p->ring[i].sig, expected_sig, 64) != 0) return 0;
    }
    return 1;
}

size_t quilt_proof_locate(const quilt_proof_t *p, uint64_t t)
{
    if (!p || !p->ring) return (size_t)-1;
    size_t start = (p->head + QUILT_PROOF_RING_CAP - p->count) % QUILT_PROOF_RING_CAP;
    for (size_t k = 0; k < p->count; k++) {
        size_t i = (start + k) % QUILT_PROOF_RING_CAP;
        if (p->ring[i].tick == t) return i;
    }
    return (size_t)-1;
}
