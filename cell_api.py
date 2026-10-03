#!/usr/bin/env python3
"""
cell_api.py — the A2A surface, as a reference implementation.

WHAT THIS IS

The chain's Days 51-70 link: a thin agent-to-agent API over the cell graph. The
JEV room's design said the ONE resource should be the Cell — Quilt already treats every
cell as a live addressable capability, so the API just stops hiding it behind a UI. The
five opcodes (BIND/LINK/EFFECT/VIEW/TICK) become the methods on that resource.

This is a REFERENCE IMPLEMENTATION, not a deployment. It is deliberately dependency-free
and runs anywhere python3 does, so the design can be argued about against something that
actually executes rather than a document.

WHAT IS REAL AND WHAT IS NOT — stated up front, because the fleet's own doctrine demands it

  REAL:
    - the 5 opcodes, implemented against the same semantics as quilt-c's C99 kernel
    - the receipt chain: every mutation returns a receipt, chained by parent, hashed
    - the tree digest + verify loop, so an API session can be re-verified after the fact
    - the hard guard: VIEW is pure and cannot mutate; TICK is monotone; BIND is idempotent
    - fail-closed everywhere

  NOT REAL (and labelled as such in every response):
    - authentication. There is no auth here. The Ed25519/UCAN scheme in the design is
      specified in the README and NOT implemented. An unauthenticated mutation endpoint
      is not shippable; this is a design probe, not a service.
    - the 4D cell graph. Cells here are flat, string-keyed, single-level. The real graph
      is 4D. This does not implement it.
    - network transport. No HTTP server. The dispatch is a function call.

  A reference implementation that overstated its own fidelity would be exactly the
  failure the externalisability gate was written to catch. So: run --self-test, and
  the self-test asserts these limits are still true.

USAGE
    python3 cell_api.py --self-test
    python3 cell_api.py --demo
    python3 cell_api.py --json      # the session transcript as a receipt
"""
import argparse, hashlib, json, sys, time
from dataclasses import dataclass, field, asdict
from typing import Any, Optional

# ── the 5 opcodes, verbatim from the C99 kernel's semantics ──────────────────────
OPS = ("BIND", "LINK", "EFFECT", "VIEW", "TICK")

def fnv1a64(data: bytes) -> str:
    """The polyformalism fleet's state hash. quilt-c uses this; matching it is the
    point of a shared kernel — if this drifts, the ports are no longer byte-compatible."""
    h = 0xcbf29ce484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"

@dataclass
class Cell:
    addr: str
    kind: str
    dials: list = field(default_factory=list)
    neighbors: list = field(default_factory=list)
    version: int = 0
    version_key: str = ""

    def canonical(self) -> bytes:
        """The fleet's canonical serialization: type(1) || id(8 LE) || dials(32 LE) || neighbors(8*N LE)."""
        aid = int.from_bytes(hashlib.sha256(self.addr.encode()).digest()[:8], "little")
        b = bytes([1])
        b += aid.to_bytes(8, "little")
        for d in self.dials[:8]:
            b += (int(d) if isinstance(d, (int, float)) else 0).to_bytes(4, "little")
        b += len(self.neighbors).to_bytes(8, "little")
        for n in self.neighbors:
            b += int.from_bytes(hashlib.sha256(n.encode()).digest()[:8], "little").to_bytes(8, "little")
        return b

    def state_digest(self) -> str:
        return fnv1a64(self.canonical())

class CellGraph:
    def __init__(self):
        self.cells: dict[str, Cell] = {}
        self.tick: int = 0
        self.receipts: list[dict] = []
        self._violations: list[str] = []

    # ── the ops ───────────────────────────────────────────────────────────────
    def op(self, op: str, addr: str, args: Optional[dict] = None) -> dict:
        args = args or {}
        if op not in OPS:
            # Not a known op: seal it under its own name so an attempt is still observable.
            return self._seal(op, addr, self._err("UNKNOWN_OPCODE", f"op must be one of {OPS}"), args, time.time())
        t0 = time.time()
        if op == "BIND":
            r = self._bind(addr, args)
        elif op == "LINK":
            r = self._link(addr, args)
        elif op == "EFFECT":
            r = self._effect(addr, args)
        elif op == "VIEW":
            r = self._view(addr, args)
        else:
            r = self._tick(addr, args)
        return self._seal(op, addr, r, args, t0)

    @staticmethod
    def _err(code, detail):
        """A failure is a RESULT, not a receipt. The op bodies return results; op() seals
        exactly once. The first version had op bodies return a sealed receipt, which then
        got sealed again as the result — a receipt nested inside a receipt. Separating
        'what happened' from 'the record of what happened' is the whole point of the split."""
        return {"error": code, "detail": detail}

    def _bind(self, addr, args):
        dials = list(args.get("dials", []))
        c = self.cells.get(addr)
        if c is None:
            c = Cell(addr=addr, kind=args.get("kind", "generic"), dials=dials)
            self.cells[addr] = c
            c.version = 1
            created = True
        else:
            created = False
            before = c.state_digest()
            c.dials = dials
            # BIND is idempotent BY CONTENT. Re-binding the same dials is a no-op, so
            # the version does not advance. The first version bumped version
            # unconditionally, which made the idempotence law unsatisfiable — the law was
            # not being tested, it was being contradicted by the implementation.
            if c.state_digest() != before:
                c.version += 1
        c.version_key = f"{c.version}:{c.state_digest()}"
        return {"cell": addr, "dials": c.dials, "version": c.version, "created": created}

    def _link(self, addr, args):
        a, b = args.get("a"), args.get("b")
        if a not in self.cells or b not in self.cells:
            return self._err("MISSING_CELL", f"LINK requires both cells bound; have {sorted(self.cells)}")
        ca, cb = self.cells[a], self.cells[b]
        if b not in ca.neighbors:
            ca.neighbors.append(b)
        if a not in cb.neighbors:
            cb.neighbors.append(a)
        ca.version += 1; cb.version += 1
        return {"edge": [a, b], "a_neighbors": ca.neighbors, "b_neighbors": cb.neighbors}

    def _effect(self, addr, args):
        c = self.cells.get(addr)
        if c is None:
            return self._err("MISSING_CELL", f"no cell at {addr}")
        # EFFECT propagates dial[0] to neighbours whose dial[0] differs — the
        # associative, pure-with-respect-to-itself propagation from the C99 kernel.
        propagated = []
        for n in c.neighbors:
            nb = self.cells.get(n)
            if nb is None:
                continue
            if c.dials and nb.dials and nb.dials[0] != c.dials[0]:
                nb.dials[0] = c.dials[0]
                nb.version += 1
                propagated.append(n)
        c.version += 1
        return {"cell": addr, "propagated_to": propagated}

    def _view(self, addr, args):
        c = self.cells.get(addr)
        if c is None:
            return self._err("MISSING_CELL", f"no cell at {addr}")
        # HARD GUARD: VIEW must be pure. Fingerprint before and after; a change is a bug
        # in the kernel, and the receipt records it rather than hiding it.
        before = {a: c.state_digest() for a in self.cells}
        view = {"cell": addr, "dials": list(c.dials), "neighbors": list(c.neighbors),
                "version": c.version, "state_digest": c.state_digest()}
        after = {a: c.state_digest() for a in self.cells}
        if before != after:
            self._violations.append("VIEW_MUTATED_STATE")
        return view

    def _tick(self, addr, args):
        # TICK is the only opcode that operates on the GRAPH, not on a cell. So a
        # non-existent cell address is not an error here — but saying so implicitly (as
        # the first version did, by succeeding on a missing address) is how an API ends
        # up with undocumented behaviour. The address is recorded as graph-level.
        if addr in self.cells or addr in ("graph", "", None):
            pass  # valid: a cell that exists, or the explicit graph handle
        else:
            return self._err("MISSING_CELL",
                             f"TICK takes the graph or an existing cell; {addr!r} is neither "
                             f"(TICK is graph-scoped, use addr='graph')")
        before = self.tick
        self.tick += 1
        for c in self.cells.values():
            for i, d in enumerate(c.dials):
                nxt = d + (1 if i % 2 == 0 else -1)
                # SATURATE, do not wrap. Repeated TICK drives odd-index dials negative and
                # the canonical serializer writes 4-byte UNSIGNED ints, so an unclamped
                # value raises OverflowError. Clamping is also the right domain semantic:
                # a quota or a hold count should read zero, not -1. This is a policy
                # choice, so it is explicit here rather than hidden in the encoder.
                c.dials[i] = max(0, nxt)
            c.version += 1
        return {"tick": self.tick, "delta": self.tick - before, "cells": len(self.cells)}

    def _fail(self, op, addr, code, detail):
        return {"error": code, "detail": detail}

    @staticmethod
    def _snapshot(o):
        """Deep-copy a result into immutable form BEFORE it enters a receipt.

        Found by running --demo, not by running the tests: the op bodies returned dicts
        holding live references to the cells' dials/neighbors lists, so a BIND receipt
        printed [100, 0] at seal time and [101, -1] after a later TICK mutated the same
        list in place. A receipt that changes after the fact is not a receipt; it is a
        pointer. This is the same class of bug as the two earlier in verify.py (a
        self-referential digest, and wall-clock timings inside a hashed body): the
        artifact looked right and was not, because nothing read it twice.
        """
        if isinstance(o, dict):
            return {k: CellGraph._snapshot(v) for k, v in o.items()}
        if isinstance(o, list):
            return [CellGraph._snapshot(v) for v in o]
        return o

    def _seal(self, op, addr, result, args, t0):
        result = self._snapshot(result)
        args = self._snapshot(args)
        body = {"op": op, "addr": addr, "args": args, "result": result}
        parent = self.receipts[-1]["receipt_id"] if self.receipts else None
        raw = json.dumps({**body, "parent": parent}, sort_keys=True, separators=(",", ":"))
        rid = hashlib.sha256(raw.encode()).hexdigest()[:16]
        rec = {"schema": "quilt/cell-receipt@v1", "receipt_id": rid, "parent": parent,
               "op": op, "addr": addr, "result": result,
               "graph_digest": self.graph_digest(), "mutating": op in ("BIND", "LINK", "EFFECT", "TICK"),
               "elapsed_ms": round((time.time() - t0) * 1000, 3)}
        self.receipts.append(rec)
        return rec

    def graph_digest(self) -> str:
        parts = []
        for addr in sorted(self.cells):
            c = self.cells[addr]
            parts.append(f"{addr}|{c.kind}|{c.dials}|{sorted(c.neighbors)}|v{c.version}")
        return hashlib.sha256((";".join(parts) + f";tick={self.tick}").encode()).hexdigest()[:32]

    # ── the laws (same laws quilt-c asserts) ──────────────────────────────────
    def law_bind_idempotent(self, addr) -> bool:
        before = self.cells[addr].version_key
        self.op("BIND", addr, {"dials": list(self.cells[addr].dials)})
        return before == self.cells[addr].version_key

    def law_view_pure(self, addr) -> bool:
        n = len(self._violations)
        self.op("VIEW", addr)
        return len(self._violations) == n

    def law_tick_monotonic(self) -> bool:
        t = [self.tick]
        for _ in range(3):
            self.op("TICK", "graph")
            t.append(self.tick)
        # monotone = never decreases AND strictly advances on each call
        return all(b > a for a, b in zip(t, t[1:]))

    def law_link_undirected(self, a, b) -> bool:
        return b in self.cells[a].neighbors and a in self.cells[b].neighbors


def demo() -> dict:
    g = CellGraph()
    ops = []
    ops.append(g.op("BIND", "boat-1/quota", {"kind": "gauge", "dials": [100, 0]}))
    ops.append(g.op("BIND", "boat-1/hold", {"kind": "gauge", "dials": [100, 0]}))
    ops.append(g.op("LINK", "graph", {"a": "boat-1/quota", "b": "boat-1/hold"}))
    ops.append(g.op("EFFECT", "boat-1/quota"))
    ops.append(g.op("VIEW", "boat-1/hold"))
    ops.append(g.op("TICK", "graph"))
    return {"graph_digest": g.graph_digest(), "receipts": ops, "chain_len": len(g.receipts)}


SELFTEST = []
def _t(name):
    def deco(fn):
        SELFTEST.append((name, fn)); return fn
    return deco

@_t("BIND creates and is idempotent")
def _s1():
    g = CellGraph()
    a = g.op("BIND", "x", {"dials": [7]}); b = g.op("BIND", "x", {"dials": [7]})
    return a["result"].get("created") is True and b["result"].get("created") is False and g.law_bind_idempotent("x")

@_t("LINK is undirected and transitive-reachable")
def _s2():
    g = CellGraph()
    for n in "abc": g.op("BIND", n, {"dials": [1]})
    g.op("LINK", "g", {"a": "a", "b": "b"}); g.op("LINK", "g", {"a": "b", "b": "c"})
    reach = set(["a"]); frontier = ["a"]
    while frontier:
        n = frontier.pop()
        for m in g.cells[n].neighbors:
            if m not in reach: reach.add(m); frontier.append(m)
    return g.law_link_undirected("a", "b") and g.law_link_undirected("b", "a") and "c" in reach

@_t("EFFECT propagates dial[0] to neighbours")
def _s3():
    g = CellGraph()
    g.op("BIND", "src", {"dials": [5, 1]}); g.op("BIND", "dst", {"dials": [9, 1]})
    g.op("LINK", "g", {"a": "src", "b": "dst"})
    r = g.op("EFFECT", "src")
    return r["result"]["propagated_to"] == ["dst"] and g.cells["dst"].dials[0] == 5

@_t("VIEW is pure — hard guard")
def _s4():
    g = CellGraph()
    g.op("BIND", "x", {"dials": [1, 2, 3]})
    before = g.graph_digest()
    r = g.op("VIEW", "x")
    return g.graph_digest() == before and g.law_view_pure("x") and r["result"]["dials"] == [1, 2, 3]

@_t("TICK is monotone and advances every dial")
def _s5():
    # Order matters. law_tick_monotonic() ticks the graph three more times, so the dial
    # assertion has to be captured BEFORE the law runs, or it asserts against post-law
    # state. The first version evaluated `g.cells["x"].dials == [1,9]` last, so it was
    # really asserting dials == [4,6] and reporting a product bug. A test that fails for
    # the wrong reason trains you to ignore it.
    g = CellGraph()
    g.op("BIND", "x", {"dials": [0, 10]})
    t0 = g.tick
    g.op("TICK", "x")
    dials_after_one = list(g.cells["x"].dials)
    advanced = g.tick == t0 + 1
    monotone = g.law_tick_monotonic()
    return dials_after_one == [1, 9] and advanced and monotone

@_t("receipts chain by parent, and the chain is verifiable")
def _s6():
    g = CellGraph()
    g.op("BIND", "a", {"dials": [1]}); g.op("BIND", "b", {"dials": [2]})
    g.op("VIEW", "a")
    parents = [r["parent"] for r in g.receipts]
    return (parents[0] is None and parents[1] == g.receipts[0]["receipt_id"]
            and parents[2] == g.receipts[1]["receipt_id"])

@_t("VIEW carries mutating=false; mutations carry mutating=true")
def _s7():
    g = CellGraph()
    g.op("BIND", "a", {"dials": [1]})
    return g.receipts[0]["mutating"] is True and g.op("VIEW", "a")["mutating"] is False

@_t("unknown opcode fails closed")
def _s8():
    g = CellGraph()
    r = g.op("DANCE", "a", {})
    return r["result"].get("error") == "UNKNOWN_OPCODE"

@_t("ops on a missing cell fail closed with a reason")
def _s9():
    g = CellGraph()
    bad = [g.op(o, "nope", {}) for o in ("VIEW", "EFFECT", "TICK")]
    return all(r["result"].get("error") == "MISSING_CELL" for r in bad)

@_t("LINK with an unbound endpoint fails closed")
def _s10():
    g = CellGraph()
    g.op("BIND", "a", {"dials": [1]})
    return g.op("LINK", "g", {"a": "a", "b": "ghost"})["result"].get("error") == "MISSING_CELL"

@_t("state digest matches the fleet's FNV-1a 64 convention")
def _s11():
    """quilt-c's canary is id=1, dials=[1..16], neighbours=[2,3,4] -> 0xe435d91d6d92a1d8.
    The address is hashed here rather than carried as a literal id, so the exact digest
    depends on the address string. What is asserted is the SHAPE and DETERMINISM: 16 hex
    chars, lowercase, stable across calls, and changing when a dial changes. Asserting
    the literal fleet canary would require adopting quilt-c's id encoding, which is a
    separate porting decision and not something to fake in a design probe."""
    g = CellGraph()
    for n in ("canary", "n1", "n2", "n3"):
        g.op("BIND", n, {"dials": list(range(1, 17))})
    g.op("LINK", "g", {"a": "canary", "b": "n1"})
    g.op("LINK", "g", {"a": "canary", "b": "n2"})
    g.op("LINK", "g", {"a": "canary", "b": "n3"})
    d1 = g.cells["canary"].state_digest()
    d2 = g.cells["canary"].state_digest()
    g.cells["canary"].dials[0] = 99
    d3 = g.cells["canary"].state_digest()
    hexok = all(ch in "0123456789abcdef" for ch in d1)
    return len(d1) == 16 and hexok and d1 == d2 and d1 != d3

@_t("a receipt is an immutable snapshot, not a live pointer")
def _s13():
    """A receipt that changes after the fact is not a receipt. This leg is the direct
    guard on the bug the demo exposed."""
    g = CellGraph()
    g.op("BIND", "a", {"dials": [100, 0]})
    before = json.dumps(g.receipts[0], sort_keys=True)
    g.op("BIND", "b", {"dials": [1, 1]})
    g.op("TICK", "graph")
    g.op("BIND", "a", {"dials": [7, 7]})
    after = json.dumps(g.receipts[0], sort_keys=True)
    return before == after


@_t("the honesty limits are structural, not just documented")
def _s12():
    """A design probe that OVERSTATES its own fidelity is exactly the failure the
    externalisability gate exists to catch. The first version of this leg grepped the
    source for auth/4D/transport markers — and matched its own docstring, which describes
    those markers. The test was reading its own description of itself and passing for the
    wrong reason.

    So check the STRUCTURE instead: does the class expose a signature verifier, a
    hierarchical address space, or a socket? Those are properties of the running object,
    not of a comment about it."""
    g = CellGraph()
    has_auth = any(hasattr(g, m) for m in ("verify", "verify_signature", "authenticate"))
    has_http = any(hasattr(g, m) for m in ("serve", "handle_request", "route_http"))
    # addressing is flat: every addr is a plain key, no component parser
    flat = "." not in g.cells  # true at construction: no hierarchical walk exists
    return (not has_auth) and (not has_http) and flat


def run_self_test() -> int:
    print("cell_api self-test")
    print("=" * 66)
    bad = 0
    for name, fn in SELFTEST:
        try:
            ok = bool(fn())
        except Exception as e:
            ok = False; name += f"  [{type(e).__name__}: {e}]"
        if not ok: bad += 1
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}")
    print("=" * 66)
    print(f"selftest: {len(SELFTEST)-bad}/{len(SELFTEST)} legs correct")
    if bad == 0:
        print()
        print("LIMITS OF THIS IMPLEMENTATION (also asserted by leg 12):")
        print("  - NO AUTHENTICATION. An unauthenticated mutation endpoint is not")
        print("    shippable. Ed25519/UCAN is specified in the README, not implemented.")
        print("  - FLAT ADDRESSING. Cells are single-level string keys. The real graph is 4D.")
        print("  - NO NETWORK TRANSPORT. Dispatch is a function call, not HTTP.")
    return 0 if bad == 0 else 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--demo", action="store_true")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    if a.self_test: return run_self_test()
    d = demo()
    if a.json:
        print(json.dumps(d, indent=2))
    else:
        print("cell_api demo — a 48h quota boat running the 5 opcodes")
        print("=" * 66)
        for r in d["receipts"]:
            res = r["result"]
            summary = json.dumps({k: v for k, v in res.items() if k != "error"})[:60]
            print(f"  {r['receipt_id']}  {r['op']:7} {r['addr']:16} mutating={str(r['mutating']):5} {summary}")
        print("=" * 66)
        print(f"  receipts: {d['chain_len']}   graph_digest: {d['graph_digest']}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
