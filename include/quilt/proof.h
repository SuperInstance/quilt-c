/* quilt/proof.h — the PROOF opcode (Phase 216 cutting-edge adoption #1).
 *
 * Idea: signed hash-linked audit chain per cell. The cell's journal
 * becomes a tamper-evident ring. Each entry is:
 *
 *   prev_hash || ed25519_sig(cell_state_at_t) || cell_state
 *
 * Adopted from:
 *   - astrid-runtime/astrid (capability-mediated audit chain)
 *   - InterSAGE (capability-aware trust substrate)
 *   - Bounded Agents / APC (formally proves Blast Radius Monotonicity)
 *
 * In a kernel-friendly build we use SHA-256 (in core) and the
 * ed25519-dalek crate when std is available. This header is
 * deliberately minimal: a ring buffer of entries, a finalize() that
 * signs the chain, and a verify() that walks it.
 *
 * The polyformalism promise: this lives behind the opcode set, not
 * the language. A PROOF cell on a Cloudflare Worker uses Workers
 * Crypto; on ESP32 it uses mbedTLS; on the host it uses OpenSSL.
 * Same 5+1 opcodes, different substrate.
 */
#ifndef QUILT_PROOF_H
#define QUILT_PROOF_H

#include <stdint.h>
#include <stddef.h>
#include "quilt/cell.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUILT_PROOF_HASH_BYTES 32      /* SHA-256 */
#define QUILT_PROOF_SIG_BYTES  64      /* ed25519 */
#define QUILT_PROOF_RING_CAP   1024    /* entries per cell */

typedef struct {
    uint8_t  prev_hash[QUILT_PROOF_HASH_BYTES];
    uint8_t  sig[QUILT_PROOF_SIG_BYTES];
    uint8_t  state_hash[QUILT_PROOF_HASH_BYTES];
    uint64_t tick;
    uint64_t version;
} quilt_proof_entry_t;

typedef struct {
    quilt_proof_entry_t *ring;     /* caller-owned; size = QUILT_PROOF_RING_CAP */
    size_t head;                   /* next write position (mod cap) */
    size_t count;                  /* entries written (saturates at cap) */
    /* The ed25519 public key used to sign entries; 32 bytes */
    uint8_t pub[32];
} quilt_proof_t;

/* ── Lifecycle ──────────────────────────────────────────────────────── */
int  quilt_proof_init(quilt_proof_t *p, quilt_proof_entry_t *ring);
void quilt_proof_free(quilt_proof_t *p);

/* Append one entry. The new entry's prev_hash is the previous entry's
 * state_hash (or all-zero for the first). The signature is computed
 * by the substrate's ed25519 implementation; in this C port we leave
 * the signature field zeroed and let the caller fill it (or use
 * `quilt_proof_append_placeholder` in test mode).
 *
 * On a real substrate (Workers, ESP32, CUDA) the sign step is
 * provided by the polyformalism's crypto binding. */
int  quilt_proof_append(quilt_proof_t *p, const quilt_value_t *state,
                         uint64_t tick, uint64_t version);

/* Verify the chain: prev_hash[i+1] == state_hash[i] for all i.
 * Returns 1 if valid, 0 if not. */
int  quilt_proof_verify(const quilt_proof_t *p);

/* Walk the chain and return the index of the entry whose tick
 * matches `t`, or (size_t)-1 if not present. */
size_t quilt_proof_locate(const quilt_proof_t *p, uint64_t t);

#ifdef __cplusplus
}
#endif

#endif /* QUILT_PROOF_H */
