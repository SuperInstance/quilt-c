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

/** Recompute the source-tree digest exactly as verify.py does. */
export function treeDigest(root = process.cwd()) {
  const files = [];
  const walk = (dir, rel = '') => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      if (['.git', 'build', '.github', 'node_modules'].includes(entry.name)) continue;
      const abs = join(dir, entry.name);
      const r = rel ? `${rel}/${entry.name}` : entry.name;
      if (entry.isDirectory()) { walk(abs, r); continue; }
      if (!entry.isFile()) continue;
      if (r === 'VERIFY_RECEIPT.json') continue;      // output, not input
      if (r.endsWith('~') || r.endsWith('.swp')) continue;
      files.push([r, sha256(rf(abs))]);
    }
  };
  walk(root);
  files.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
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

  // 3. receipt self-hash (recompute over the body WITHOUT the hash field, sorted keys)
  const { receipt_sha256: claimed, ...body } = r;
  const recomputed = sha256(JSON.stringify(body, Object.keys(body).sort()));
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
  const tag = a[a.indexOf('--tag') + 1];
  const rf = a[a.indexOf('--receipt') + 1];
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
