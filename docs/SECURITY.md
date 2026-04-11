# Security and vulnerability handling

This project is a local-first document/archive engine, not a network service by itself. That lowers exposure, but it does **not** remove security risk. If the tool mishandles paths, locks, metadata, or archive contents, users can still lose data or trust the wrong result.

## Security goals

Prioritize:

1. data integrity
2. predictable archive behavior
3. safe filesystem interactions
4. clear operator expectations when something goes wrong

## Threats worth caring about

- path handling bugs that escape the intended archive/workspace root
- corrupted or inconsistent archive metadata
- incorrect version/lock handling that causes accidental overwrite or false confidence
- unsafe parsing in import extensions
- dependency vulnerabilities in SQLite/OpenSSL/Zstd or other build/runtime components
- supply-chain drift between source, build environment, and released binaries

## Reporting a vulnerability

Until a dedicated security contact exists, report privately to the maintainer through the repository hosting contact path or direct maintainer contact.

When reporting, include:

- affected version / commit
- platform and build details
- clear reproduction steps
- impact assessment (data loss, integrity issue, path escape, crash, etc.)
- proof-of-concept only as needed

Please avoid dumping unreviewed exploit details into public issues first when the issue could put real user archives at risk.

## Triage expectations

Reasonable handling target:

- acknowledge report
- reproduce and classify impact
- fix or document mitigation
- publish a small advisory note in release notes when the issue is real and user-relevant

## Secure development expectations

Lightweight but non-negotiable:

- keep filesystem operations rooted and explicit
- treat archive metadata as untrusted input during reads
- fail loudly on ambiguity or inconsistent state
- add regression tests for security-relevant bugs
- document dangerous commands and operator footguns
- be explicit about integrity/recovery limits; do not imply the archive is self-healing when it is not

## Integrity and recovery posture

For the current MVP, the main operational truth is simple:

- workspace drift detection exists
- readonly workspace repair exists
- full archive-internal verification/repair does not exist yet

That means backup quality is part of the security/integrity story.

See `docs/ARCHIVE_INTEGRITY_AND_RECOVERY.md` for the concrete operator guidance:
- minimum backup set
- restore drill
- expected failure signals
- current limits of `status`, `inspect`, and `repair`

## What this project does not claim

This repository currently does **not** claim:

- formal security certification
- 24/7 incident response
- guaranteed vulnerability response SLA
- tamper-proof runtime or signed release pipeline

That is fine for a prototype, as long as the docs are honest about it.
