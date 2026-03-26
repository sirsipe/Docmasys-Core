# Backlog

Lightweight product/work tracker for Docmasys-Core.

## How to use this file

Keep it blunt:

- one line per item when possible
- prefer concrete outcomes over vague ideas
- move done items to the changelog/release notes, not into a graveyard here
- if something needs design thinking, add a short note under the item instead of writing a novel

Suggested priorities:

- `P0` critical correctness, data safety, security
- `P1` important product/workflow improvements
- `P2` useful but non-urgent
- `P3` nice-to-have / experiments

Suggested statuses:

- `todo`
- `doing`
- `blocked`
- `done`

## Template

```text
- [status] P1 short title
  - Why: user/problem impact
  - Done when: observable outcome
  - Notes: optional constraints / links / commands
```

## Current backlog

- [todo] P0 document archive integrity and recovery expectations
  - Why: if Docmasys is used for document retention, operators need to know what to back up, how to verify, and what corruption looks like.
  - Done when: docs explain minimum backup set, restore drill, and integrity checks.

- [todo] P0 harden readonly workspace guarantees
  - Why: readonly-copy and readonly-symlink flows protect users from accidental edits, but edge cases and platform differences matter.
  - Done when: behavior is validated on Linux/macOS/Windows or clearly documented as unsupported where needed.

- [todo] P1 add ignore/include rules for imports
  - Why: real trees contain build outputs, temp files, and junk that should not become archive history.
  - Done when: import supports predictable filtering with tests and docs.

- [todo] P1 improve inspect/reporting output
  - Why: operators need a fast view of files, versions, locks, and workspace state without scraping multiple commands.
  - Done when: inspect provides a concise human-readable summary and/or machine-readable mode.

- [todo] P1 define a simple release checklist
  - Why: repeatable builds and clear release contents matter more than vibes.
  - Done when: tagged releases consistently include build/test results, docs, and artifact notes.

- [todo] P2 add machine-readable dependency and SBOM generation
  - Why: manual expectations are fine at prototype stage, but generated outputs scale better.
  - Done when: release process can emit at least one SBOM artifact (for example SPDX or CycloneDX).

- [todo] P2 evaluate permission/admin model for unlock and future destructive operations
  - Why: `unlock` is intentionally blunt; that is useful until it bites someone.
  - Done when: policy is explicit, even if enforcement stays outside the core.

- [todo] P3 add export/report helpers for archive contents
  - Why: users may want inventories, relation graphs, or version summaries without writing custom tooling.
  - Done when: one or two practical reporting commands exist.
