#!/usr/bin/env python3
"""
quilt-c verify — the dependency-closed artifact.

Runs every suite, counts assertions, and emits a receipt.
No arguments. No network. No dependencies beyond a C99 compiler and python3.

Exit 0 = verified, exit 1 = failed (fail-closed).
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
if os.path.basename(ROOT) == "build":
    ROOT = os.path.dirname(ROOT)
os.chdir(ROOT)

SUITES = ["test", "test-proof", "test-route", "test-crdt", "test-world", "test-time", "test-quf"]
PAT = re.compile(r"(\d+)\s+passed,\s+(\d+)\s+failed")

def run_suite(name):
    t0 = time.time()
    p = subprocess.run(["make", name], capture_output=True, text=True)
    out = p.stdout + p.stderr
    m = PAT.findall(out)
    passed = sum(int(a) for a, _ in m)
    failed = sum(int(b) for _, b in m)
    return {
        "suite": name,
        "exit": p.returncode,
        "passed": passed,
        "failed": failed,
        "seconds": round(time.time() - t0, 3),
    }

def tree_digest():
    """FNV-style content digest over the source tree: order-independent by sort,
    so it is a stable identity of the artifact, not of the checkout order."""
    h = hashlib.sha256()
    files = []
    for dirpath, dirnames, filenames in os.walk("."):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", ".github")]
        for fn in sorted(filenames):
            p = os.path.join(dirpath, fn)
            rel = os.path.relpath(p, ".")
            if rel.startswith(("build/", "./build/")):
                continue
            # The receipt is the OUTPUT of this script, not an input. Including it
            # would make the digest self-referential and therefore non-deterministic
            # across runs. Exclude it (and any editor droppings).
            if rel in ("VERIFY_RECEIPT.json", "verify_receipt.json"):
                continue
            if rel.startswith(".git"):
                continue
            try:
                with open(p, "rb") as f:
                    files.append((rel, hashlib.sha256(f.read()).hexdigest()))
            except OSError:
                pass
    for rel, dig in sorted(files):
        h.update(rel.encode())
        h.update(dig.encode())
    return h.hexdigest(), len(files)

def main():
    print("quilt-c verify")
    print("=" * 60)
    results = []
    for s in SUITES:
        r = run_suite(s)
        results.append(r)
        status = "ok " if r["exit"] == 0 and r["failed"] == 0 else "FAIL"
        print(f"  [{status}] {r['suite']:12} {r['passed']:>5} passed  {r['failed']:>3} failed  {r['seconds']:>6.2f}s")

    total_pass = sum(r["passed"] for r in results)
    total_fail = sum(r["failed"] for r in results)
    ok = all(r["exit"] == 0 and r["failed"] == 0 for r in results)
    digest, nfiles = tree_digest()

    # The receipt is a claim about the artifact, so it must be reproducible:
    # two people running verify on the same tree get the same receipt_sha256.
    # Wall-clock timings are diagnostics, not claims, so they are printed but
    # deliberately kept out of the hashed body.
    receipt = {
        "schema": "quilt-c/verify-receipt@v1",
        "verdict": "VERIFIED" if ok else "FAILED",
        "assertions_passed": total_pass,
        "assertions_failed": total_fail,
        "suites": len(results),
        "source_tree_sha256": digest,
        "source_files": nfiles,
        "suites_detail": [
            {"suite": r["suite"], "exit": r["exit"], "passed": r["passed"], "failed": r["failed"]}
            for r in results
        ],
    }
    body = json.dumps(receipt, indent=2, sort_keys=True)
    rdig = hashlib.sha256(body.encode()).hexdigest()
    receipt["receipt_sha256"] = rdig

    print("=" * 60)
    print(f"  verdict:            {receipt['verdict']}")
    print(f"  assertions passed:  {total_pass}")
    print(f"  assertions failed:  {total_fail}")
    print(f"  source tree sha256: {digest[:32]}...")
    print(f"  source files:       {nfiles}")
    print(f"  receipt sha256:     {rdig}")
    print("=" * 60)

    with open("VERIFY_RECEIPT.json", "w") as f:
        f.write(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print("wrote VERIFY_RECEIPT.json")

    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
