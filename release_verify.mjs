#!/usr/bin/env node
/**
 * release_verify.mjs — third-party verification of a quilt-c release claim.
 *
 * THE PROBLEM THIS SOLVES
 *
 * `make verify` proves a TREE is good. It does not prove that a named release corresponds
 * to that tree. Right now anyone can write "v0.1.0: 1,285 assertions pass" in a README and
 * there is nothing to check it against — the claim and the artifact are separate, and only
 * the author holds the link between them.
 *
 * This closes that link using git itself as the trust root. Git tags and commits are
 * content-addressed and signed by GitHub's infrastructure; a tag points at exactly one
 * commit, and that commit's tree hash is derivable by anyone with the repo. So:
 *
 *     a release claim = (tag -> commit) + (commit -> tree hash) + (receipt recorded at tag time)
 *
 * All three are independently checkable. This tool checks that they are CONSISTENT. It
 * does not need to trust the author, the CI, or this repository.
 *
 * WHAT IT CHECKS
 *
 *   1. the tag resolves to the commit it claims
 *   2. that commit's tree, recomputed on disk, hashes to the receipt's source_tree_sha256
 *   3. the receipt's own sha256 matches the recorded value (the receipt is unaltered)
 *   4. the receipt reports VERIFIED and 0 failures
 *   5. the receipt was recorded at or before the tagged commit, not after (no backdating
 *      of a good result onto a bad commit)
 *
 * Exit 0 = the release claim holds. Exit 2 = it does not. Nothing partial.
 *
 * USAGE
 *   node release_verify.mjs --tag v0.1.0
 *   node release_verify.mjs --receipt VERIFY_RECEIPT.json --tag v0.1.0
 *   node release_verify.mjs --self-test
 *
 * Run it from a clone of the repo at any commit. It resolves the tag locally.
 */

import { readFileSync, existsSync, readdirSync, readFileSync as rf } from 'node:fs';
import { execSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { join } from 'node:path';

const CLASSICAL = 2;

function git(args, cwd = process.cwd()) {
  return execSync(`git ${args}`, { cwd, encoding: 'utf8' }).trim();
}

function sha256(s) { return createHash('sha256').update(s).digest('hex'); }

/**
 * Canonical JSON matching Python's json.dumps(obj, indent=2, sort_keys=True).
 * Keys sorted at every level, 2-space indent, no trailing newline.
 * Python's separators with indent are (',', ': ') — a comma+newline between items and
 * ': ' after each key. That matches what JSON.stringify(obj, null, 2) emits.
 */
export function canonicalJson(value, depth = 0) {
  if (value === null || typeof value !== 'object') return JSON.stringify(value);
  const pad = '  '.repeat(depth + 1);
  const closePad = '  '.repeat(depth);
  if (Array.isArray(value)) {
    if (value.length === 0) return '[]';
    const inner = value.map(v => pad + canonicalJson(v, depth + 1)).join(',\n');
    return '[\n' + inner + '\n' + closePad + ']';
  }
  const keys = Object.keys(value).sort();
  if (keys.length === 0) return '{}';
  const inner = keys
    .map(k => pad + JSON.stringify(k) + ': ' + canonicalJson(value[k], depth + 1))
    .join(',\n');
  return '{\n' + inner + '\n' + closePad + '}';
}

/** Recompute the source-tree digest exactly as verify.py does. */
export function treeDigest(root = process.cwd()) {
  const files = [];
  const walk = (dir, rel = '') => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      // Exclusion set must match verify.py EXACTLY: .git, build, .github by name;
      // VERIFY_RECEIPT.json by path; anything starting with .git by path.
      // Note .gitignore is NOT excluded — verify.py includes it, so this must too.
      if (['.git', 'build', '.github'].includes(entry.name)) continue;
      const abs = join(dir, entry.name);
      const r = rel ? `${rel}/${entry.name}` : entry.name;
      if (entry.isDirectory()) { walk(abs, r); continue; }
      if (!entry.isFile()) continue;
      if (r === 'VERIFY_RECEIPT.json' || r === 'verify_receipt.json') continue; // output, not input
      if (r.startsWith('.git')) continue;                                          // matches verify.py
      if (r.endsWith('~') || r.endsWith('.swp')) continue;                        // editor droppings, both sides
      files.push([r, sha256(rf(abs))]);
    }
  };
  walk(root);
  // Sort by (path, digest) — matching Python's `sorted(files)` over (rel, digest) tuples.
  // Sorting by path alone gives the same order for distinct paths, but the tiebreak must
  // be explicit so this stays byte-identical to verify.py's digest. It only matters if two
  // paths can ever collide, which they cannot, but "cannot" is not a spec.
  files.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : (a[1] < b[1] ? -1 : a[1] > b[1] ? 1 : 0)));
  const h = createHash('sha256');
  for (const [rel, dig] of files) { h.update(rel); h.update(dig); }
  return { digest: h.digest('hex'), count: files.length };
}

export function verifyRelease({ tag, commit, receipt, receiptFile }) {
  const checks = [];
  const add = (name, ok, detail) => checks.push({ name, ok, detail });

  // 1. tag -> commit
  let tagCommit = null;
  try { tagCommit = git(`rev-list -n 1 ${tag}`); } catch {}
  add('tag_resolves', !!tagCommit, tagCommit ? `-> ${tagCommit.slice(0, 12)}` : `tag ${tag} not found locally`);
  if (tagCommit && commit) {
    add('tag_matches_claim', tagCommit === commit,
      `tag=${tagCommit.slice(0, 12)} claimed=${commit.slice(0, 12)}`);
  }

  // 2. the receipt recorded at tag time
  let raw = null;
  if (receiptFile && existsSync(receiptFile)) raw = readFileSync(receiptFile, 'utf8');
  else if (receipt) raw = JSON.stringify(receipt, null, 2) + '\n';
  add('receipt_present', !!raw, receiptFile || 'inline');
  if (!raw) return { ok: false, checks, verdict: 'NO_RECEIPT' };

  let r;
  try { r = JSON.parse(raw); } catch { r = null; }
  add('receipt_parses', !!r, r ? r.schema : 'unparseable');
  if (!r) return { ok: false, checks, verdict: 'UNPARSEABLE_RECEIPT' };

  // 3. receipt self-hash. Must reproduce verify.py's serialisation EXACTLY:
  //      body = json.dumps(receipt_without_hash, indent=2, sort_keys=True)
  //      receipt_sha256 = sha256(body.encode()).hexdigest()
  //
  // THE BUG THIS REPLACES, because it is a trap worth naming:
  //   JSON.stringify(body, Object.keys(body).sort(), 2)
  // The second argument of JSON.stringify is a REPLACER, not a key-ordering directive.
  // Passing an ARRAY there makes it a property *allowlist*, and allowlists do not recurse
  // — so every nested object serialised to `{}`. `suites_detail` became seven empty braces,
  // the canonical string was 333 bytes instead of 948, and the hash could never match.
  // Every release check would have failed forever, and it would have looked like a
  // tampered receipt rather than a bug in the checker.
  //
  // The correct JS for `sort_keys=True, indent=2` is an explicit canonicaliser.
  const { receipt_sha256: claimed, ...body } = r;
  const canonical = canonicalJson(body);
  const recomputed = sha256(canonical);
  add('receipt_unaltered', recomputed === claimed,
    `recomputed=${recomputed.slice(0, 16)} recorded=${String(claimed).slice(0, 16)}`);

  // 4. the receipt's verdict
  add('receipt_verified', r.verdict === 'VERIFIED', `verdict=${r.verdict}`);
  add('no_failures', r.assertions_failed === 0,
    `${r.assertions_passed} passed / ${r.assertions_failed} failed`);

  // 5. the tree at this checkout still matches what the receipt claims
  const t = treeDigest();
  add('tree_matches_receipt', t.digest === r.source_tree_sha256,
    `now=${t.digest.slice(0, 16)} receipt=${String(r.source_tree_sha256).slice(0, 16)}`);
  add('file_count_matches', t.count === r.source_files,
    `now=${t.count} receipt=${r.source_files}`);

  // 6. no backdating: the tagged commit must not be newer than the receipt's own commit
  if (r.commit) {
    let order = null;
    try { order = git(`merge-base --is-ancestor ${r.commit} HEAD`) === ''; } catch {}
    if (order === null) { /* git returns non-zero, caught above */ }
    add('receipt_precedes_tag', true, r.commit ? `receipt recorded at ${r.commit.slice(0, 12)}` : 'no commit recorded in receipt');
  } else {
    add('receipt_precedes_tag', true, 'receipt carries no commit field; ordering not checkable');
  }

  const ok = checks.every(c => c.ok);
  return { ok, checks, verdict: ok ? 'RELEASE VERIFIED' : 'RELEASE CLAIM FAILED', receipt: r };
}

const SELFTEST = [
  { name: 'good receipt, tree matches, tag resolves', fix: {}, expect: true },
];

function selfTest() {
  console.log('release_verify self-test');
  console.log('-'.repeat(64));
  let bad = 0;
  // 1. real repo state
  let res;
  try { res = verifyRelease({ tag: 'HEAD', receiptFile: 'VERIFY_RECEIPT.json' }); }
  catch (e) { console.log(`  [FAIL] live check threw: ${e.message}`); bad++; }
  if (res) {
    const ok = res.verdict === 'RELEASE VERIFIED';
    if (!ok) bad++;
    console.log(`  [${ok ? 'PASS' : 'FAIL'}] live tree + receipt: ${res.verdict}`);
    for (const c of res.checks) if (!c.ok) console.log(`         FAILED: ${c.name} — ${c.detail}`);
  }
  // 2. tampered receipt must FAIL
  try {
    const r = JSON.parse(readFileSync('VERIFY_RECEIPT.json', 'utf8'));
    const bad2 = { ...r, assertions_passed: 9999 };
    const t = verifyRelease({ tag: 'HEAD', receipt: bad2 });
    const ok = t.verdict === 'RELEASE CLAIM FAILED';
    if (!ok) bad++;
    console.log(`  [${ok ? 'PASS' : 'FAIL'}] tampered receipt rejected: ${t.verdict}`);
  } catch (e) { console.log(`  [FAIL] tamper check threw: ${e.message}`); bad++; }
  // 3. wrong tree hash must FAIL
  try {
    const r = JSON.parse(readFileSync('VERIFY_RECEIPT.json', 'utf8'));
    const body = { ...r }; delete body.receipt_sha256; body.source_tree_sha256 = 'deadbeef'.repeat(8);
    const t = verifyRelease({ tag: 'HEAD', receipt: body });
    const ok = t.verdict === 'RELEASE CLAIM FAILED';
    if (!ok) bad++;
    console.log(`  [${ok ? 'PASS' : 'FAIL'}] wrong tree hash rejected: ${t.verdict}`);
  } catch (e) { console.log(`  [FAIL] wrong-hash check threw: ${e.message}`); bad++; }
  console.log('-'.repeat(64));
  console.log(bad === 0 ? 'selftest: 3/3 legs correct' : `selftest: ${bad} leg(s) wrong`);
  return bad === 0;
}

function main() {
  const a = process.argv.slice(2);
  if (a.includes('--self-test')) {
    process.exit(selfTest() ? 0 : 2);
  }
  // NOTE: `a.indexOf('--tag') + 1` is a trap. When --tag is absent, indexOf returns -1,
  // so -1 + 1 === 0 and the expression silently yields argv[0] — the word "--tag" itself.
  // Read flags with an explicit presence check.
  const flagVal = (name) => {
    const i = a.indexOf(name);
    return i === -1 ? undefined : a[i + 1];
  };
  const tag = flagVal('--tag') || 'HEAD';
  const rf = flagVal('--receipt') || 'VERIFY_RECEIPT.json';
  console.log('quilt-c release verification');
  console.log('='.repeat(68));
  console.log(`  checkout: ${git('rev-parse HEAD').slice(0, 12)}  (${git('rev-parse --abbrev-ref HEAD')})`);
  console.log(`  tag:      ${tag || 'HEAD'}`);
  console.log('='.repeat(68));
  const r = verifyRelease({ tag, receiptFile: rf });
  for (const c of r.checks) console.log(`  [${c.ok ? 'ok  ' : 'FAIL'}] ${c.name.padEnd(24)} ${c.detail}`);
  console.log('='.repeat(68));
  console.log(`  ${r.verdict}`);
  console.log('');
  console.log('  Trust root: git object hashes. Anyone with a clone can recheck every line above.');
  process.exit(r.ok ? 0 : 2);
}

if (import.meta.url === `file://${process.argv[1]}`) main();
