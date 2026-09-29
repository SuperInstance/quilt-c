# Commit signing — the fleet's first verifiable author identity

**As of 2026-09-29, 0 of 791 commits in the active layer carried a cryptographic signature.**
Every author name in this substrate — `Z User`, `CCC`, `Claude`, `Mavis Agent`, `The Cowboy` —
was a string the committer chose. An outsider had no way to bind a commit to an author.

This key binds commits to a key. It is the cheap half of the fix. The expensive half is
per-agent GitHub Apps, which needs a manifest and a webhook and is Casey's call.

## The key

```
fingerprint  027CF265A31BA1F1E71FB850164D784B8401E9F5
type         ed25519 [SC] — sign only
uid          SuperInstance Fleet <mavis@superinstance.local>
expires      never
```

Public key: [`fleet-signing-key.asc`](./fleet-signing-key.asc) — armored, 417 bytes.

**This is a FLEET key, not a per-agent identity.** It proves a commit came from a holder of
this key. It does **not** distinguish keeper from Mavis from Claude from CCC — that requires
per-agent Apps, one per agent, each installed with least-privilege scope. Do not read a good
signature as "this was the keeper."

## Verify

```sh
git clone https://github.com/SuperInstance/quilt-c.git
cd quilt-c
gpg --keyserver hkps://keys.openpgp.org --recv-keys 027CF265A31BA1F1E71FB850164D784B8401E9F5
git log --show-signature -1
```

Or without touching your keyring:

```sh
git log -1 --format='%H %G? %GS'
```

- `%G?` = `G` means good signature
- `%GS` = the signer uid

Note: the keyserver is not reachable from every sandbox. If `--recv-keys` fails, fetch the
armored key from `fleet-signing-key.asc` in this repo and `gpg --import` it locally.

## Why the negative control matters

A signing key you cannot test is not a signing key. Both cases were confirmed:

| payload | result |
|---|---|
| the correct fleet key line | `Good signature` |
| the same line with one word changed | `BAD signature` |

A verifier that says "Good" to both is worse than no verifier.

## The full attribution problem, measured

See [the attribution audit](https://github.com/SuperInstance/quilt-research-canons/blob/main/research/attribution-audit-2026-09-29.md):
791 commits, 18 repos, 0 signed, 12 self-asserted author names, 1 GitHub account.

That is the number that belongs beside the externalisability audit's "0 of 10 sealed
predictions decidable by a stranger." The substrate has no externally verifiable identity to
anchor a verdict to. This key is the first one.
