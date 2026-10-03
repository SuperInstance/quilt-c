---
name: quilt-c
description: Compile, verify, and embed the quilt-c cell-fabric runtime (C99) — 5+1 opcodes, byte-exact polyformalism, fail-closed VERIFY_RECEIPT.json. Use when writing fabric code, porting cells, or needing a receipted compute kernel.
---

# quilt-c — the fabric floor

The Quilt cell-fabric runtime in C99. One header (`include/quilt/cell.h`), one
runtime file (`src/engine.c`), zero dependencies beyond a C99 compiler + python3.
Byte-exact with the whole polyformalism (Python/Rust/Go/Zig/JS/TS/...): the same
cell produces the same state hash in every language.

## Verify before you trust (one command)

```sh
git clone https://github.com/SuperInstance/quilt-c.git && cd quilt-c
make verify   # 1,285 assertions, 7 suites, fail-closed
```

Writes `VERIFY_RECEIPT.json` (schema `quilt-c/verify-receipt@v1`): verdict,
assertion counts, sha256 of the source tree, sha256 of the receipt itself.
Re-run it and compare the tree hash — if the source changed, the hash changes.
A receipt you cannot re-derive is a claim, not a receipt.

## The 5+1 opcodes

```
BIND(cell, dials)   # set dials, idempotent
LINK(c1, c2)        # add an undirected edge
EFFECT(cell)        # propagate dial[0] to neighbors
VIEW(cell)          # return dials (pure)
TICK(fabric)        # advance all dials by 1, alternating direction
FORGET(cell)        # complete removal
```

Canonical serialization: `type(1) || id(8 LE) || dials(32 LE) || neighbors(8*N LE)`.
State hash: FNV-1a 64-bit. Ground truth: test cell (id=1, dials=[1..16],
neighbors=[2,3,4]) → `0xe435d91d6d92a1d8` — byte-exact everywhere.

## The 5 laws (tested in tests/test_engine.c)

BIND idempotence · LINK transitivity · VIEW purity · TICK monotonicity ·
FORGET completeness. `make` builds `build/libquilt-c.a`; `make test` runs the
38-assertion conformance suite.

## Embedding (C)

```c
#include <quilt/cell.h>
quilt_engine_t e; quilt_cell_t cells[16];
quilt_engine_init(&e, cells, 16);
quilt_bind(&e, "a", quilt_v_int(2));
/* link, effect, view, tick, forget ... */
quilt_engine_free(&e);
```

## Gotchas

- Byte-exactness is the contract: any port that changes the serialization or
  hash is WRONG even if its own tests pass. Check against `0xe435d91d6d92a1d8`.
- `make verify` is fail-closed — nonzero exit on ANY suite failure. Do not
  ignore rc.
- Educational root: the Quilt Charter (quilt-claude-charts/QUILT_CHARTER.md).

## Neighbors

quilt-tui (browse the graph) · quilt-canvas-tui (fabric in a TUI, ports over
this kernel) · AI-Writings/algebra.md (the WAL doctrine this receipt shape
comes from) · git-agent (quilt_emit, the git isomorphism).
