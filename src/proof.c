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
    /* The 32-byte digest is just the 64-bit FNV-1a written 4 times
     * with the cell-value tag; not crypto-grade, but a deterministic
     * fingerprint for the kernel floor. Real substrates swap this. */
    uint64_t h = fnv1a64(v, sizeof(*v));
    memset(out, 0, 32);
    for (int i = 0; i < 4; i++) {
        uint64_t slice = h + (uint64_t)i;
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
    return 0;
}

void quilt_proof_free(quilt_proof_t *p)
{
    if (!p) return;
    p->ring = NULL;
    p->head = 0;
    p->count = 0;
}

int quilt_proof_append(quilt_proof_t *p, const quilt_value_t *state,
                        uint64_t tick, uint64_t version)
{
    if (!p || !p->ring || !state) return -1;
    /* prev_hash: the *chronologically* previous entry's state_hash.
     * Before the ring is full, that's the entry at head-1.
     * After wraparound, the ring holds only the last CAP entries; the
     * next append's predecessor is still the most recently written
     * entry, which is at (head-1) mod cap. The ring's circular
     * nature means head-1 is always the last write (the ring stores
     * state_hash *at the time of write*, not at the time of overwrite). */
    if (p->count == 0) {
        memset(p->ring[p->head].prev_hash, 0, 32);
    } else {
        size_t prev = (p->head + QUILT_PROOF_RING_CAP - 1) % QUILT_PROOF_RING_CAP;
        memcpy(p->ring[p->head].prev_hash,
               p->ring[prev].state_hash, 32);
    }
    /* sig: zeroed in this C port; real substrates fill it */
    memset(p->ring[p->head].sig, 0, 64);
    /* state_hash: fingerprint of the current cell state */
    hash_state(state, p->ring[p->head].state_hash);
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
