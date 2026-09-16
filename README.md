# ⚙️ quilt-c

> **The Quilt cell-fabric runtime in C99.** Part of the [polyformalism](https://github.com/SuperInstance/quilt-claude-charts/blob/main/QUILT_CHARTER.md) — the same cell model, expressed in 12+ languages, byte-exact compatible.

<p align="center">
  <img src="https://img.shields.io/badge/license-Apache--2.0-blue.svg" alt="Apache-2.0">
  <img src="https://img.shields.io/badge/language-C99-blue.svg" alt="C99">
  <img src="https://img.shields.io/badge/hash-0xe435d91d6d92a1d8-brightgreen.svg" alt="byte-exact">
</p>

## ✦ Why this port exists

The C99 port of the Quilt. The C99 port is distinctive because of its position in the language hierarchy — `gcc-compileable, C99-idiomatic, byte-exact with the rest of the polyformalism.

## ✦ The 5 opcodes

```
BIND(cell, dials)   # set dials, idempotent
LINK(c1, c2)        # add an undirected edge
EFFECT(cell)        # propagate dial[0] to neighbors
VIEW(cell)          # return dials
TICK(fabric)        # advance all dials by 1, alternating direction
```

The canonical serialization is `type(1) || id(8 LE) || dials(32 LE) || neighbors(8*N LE)`. The state hash is FNV-1a 64-bit. The test cell (id=1, dials=[1..16], neighbors=[2,3,4]) produces `0xe435d91d6d92a1d8` byte-exactly.

## ✦ The full polyformalism

| Lang | Hash | Tests |
|------|------|-------|
| Python 3 | ✓ | 7/7 |
| C99 (C99) | ✓ | manual |
| Rust | ✓ | 6/6 |
| Go | ✓ | 7/7 |
| Zig | ✓ | 7/7 |
| Mojo | ✓ | ref |
| Verilog | ✓ | manual |
| VHDL | ✓ | manual |
| JavaScript | ✓ | live |
| TypeScript | ✓ | 5/5 |

## ✦ The educational root

Every port points back to the [Quilt Charter](https://github.com/SuperInstance/quilt-claude-charts/blob/main/QUILT_CHARTER.md) — the educational root document.

## ✦ See also

- [The Quilt Charter](https://github.com/SuperInstance/quilt-claude-charts/blob/main/QUILT_CHARTER.md)
- [quilt-claude-charts](https://github.com/SuperInstance/quilt-claude-charts) — protocol + 3 charts
- [AI-Writings](https://github.com/SuperInstance/AI-Writings) — the canon (230+ papers)
- [live-canon.superinstance.dev](https://live-canon.superinstance.dev) — the live worker
- [quilt-cowboy](https://github.com/SuperInstance/quilt-cowboy) — the writers' room

## ✦ License

Apache-2.0. Free as in freedom. See [LICENSE](./LICENSE).

---

**The cell is a struct. The formula is a function pointer. The reactive engine is a recursive walk. The kernel is the runtime. C is the floor.**

---

## ✦ Build

```sh
make            # builds build/libquilt-c.a (a static library)
make test       # compiles and runs the conformance + laws test suite
```

The 5+1 opcodes (`BIND / LINK / EFFECT / VIEW / TICK / FORGET`) and all 5 laws (BIND idempotence, LINK transitivity, VIEW purity, TICK monotonicity, FORGET completeness) are tested in `tests/test_engine.c`. The test target ships with **38 assertions, all green** on C99.

The public API is one header: [`include/quilt/cell.h`](include/quilt/cell.h). The runtime is one file: [`src/engine.c`](src/engine.c). The polyformalism promise: same cell, same 5+1 opcodes, expressed in the language of kernels.

```c
#include <quilt/cell.h>

quilt_engine_t e;
quilt_cell_t cells[16];
quilt_engine_init(&e, cells, 16);

quilt_bind(&e, "a", quilt_v_int(2));
quilt_bind(&e, "b", quilt_v_int(3));
/* ... link, effect, view, tick, forget ... */

quilt_engine_free(&e);
```

`make` produces `build/libquilt-c.a` (a static library) and `build/test_engine` (the test binary). No external dependencies. C99. Runs in kernel space.
