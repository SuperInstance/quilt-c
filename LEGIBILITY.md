## What this is

A read-only legibility pass. **No existing file is modified** — this PR only adds
`LEGIBILITY.md`, and it is trivially deletable. Close it and nothing else changes.

### Why

A census of 100 fleet repos (`SuperInstance/quilt-research-canons/projects/fleet-legend/`)
graded every repo against five obligations. This one already passes entry (L1, L2); it
fails the two that only cost something once a reader is already inside:

- **L4 — what this does NOT do.** Missing in 55 of 100 repos.
- **L5 — what to do when it fails.** Missing in 59 of 100 repos.

### What is in here, and where each line came from

7 finding(s), each read out of the repository and each carrying its evidence. Nothing
is inferred from the README, because the README is the thing being fixed.

| finding | evidence |
|---|---|
| A CI workflow exists (2 file(s), e.g. `.github/workflows/ci.yml`), but which events it runs on and what it actually executes are decided inside that file, not here | `.github/workflows/ci.yml` exists in the tree |
| It ships 7 test file(s) (e.g. `tests/test_crdt.c`); what runs them is not recorded anywhere in the tree | `tests/test_crdt.c` and 6 other path(s) match the test pattern |
| It carries a license (`LICENSE`) | `LICENSE` |
| A Python build file exists but the test files in this repository are not Python, so `pytest` would collect nothing | build file present; no `test_*.py`/`*_test.py` in the tree |
| A `Makefile` exists but it defines no `test` target, so there is no canonical command to point a reader at | read from `Makefile` |
| 3+ source file(s) declare themselves incomplete, so parts of the surface are not finished — `cell_api.py`: *specified in the README and NOT implemented. An unauthenticated mutation endpoint*; `src/proof.c`: *uint64_t nonce = p->count - p->count;  /* placeholder */*; `src/world.c`: */* Stub: write a placeholder file so the C-side test passes.* | read from the files named above |
| Error-raising calls are not collected in one place: 1 call site appears across 16 files (`release_verify.mjs`:221). Nothing in the repository treats them as a set, so a reader who hits one has to grep for it | read 16 of 26 (a sample, so this is a lower bound) source file(s) in the tree; grep: `raise|throw|panic!|log.Fatal|process.exit` |

### What we deliberately did NOT write

- **Failure modes (L5).** 1 error-raising call site exist in the source (16 of 26 (a sample, so this is a lower bound) file(s) read), but the *message a user sees* and *what to do about each one* are not derivable from a file listing. Write the two or three that actually happen. A human has to supply these; guessing them is how a completer invents a failure mode.

A completer that invents a receipt or a failure mode produces a confident lie, and a
confident lie is worse than a blank space, because a reader cannot tell it from a real
limitation. Where a fact was not derivable, this file says so instead of filling the gap.

### If you want to accept part of this

Take the table and ignore the rest. Every row is a predicate over the file listing or over
named source lines, so disagreeing with a row costs you one `ls` or one `grep` — say so in
a review comment and the line gets corrected or dropped.

Reviewed with tooling from `SuperInstance/quilt-research-canons/projects/fleet-legend/`.
