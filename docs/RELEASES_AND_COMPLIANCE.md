# Releases and compliance notes

This is a lightweight, engineering-first note for release hygiene and CRA-adjacent documentation.

It is **not** a legal claim that the project is in scope for the Cyber Resilience Act, nor a declaration of conformity. It is just the sane paperwork/process skeleton you would want if the project matures.

## Release expectations

For each release, try to have:

- version/tag
- short summary of notable changes
- build/test status
- supported/known-tested platform notes
- dependency summary or SBOM artifact
- security-relevant fixes called out explicitly
- any migration or archive compatibility notes

## Recommended release artifact set

Good enough for now:

- source code tag
- generated binary or clear build instructions
- release notes
- dependency/SBOM note
- checksum for published binary artifacts, if binaries are published

Better later:

- signed tags and/or signed artifacts
- machine-readable SBOM
- provenance/attestation from CI

## Operator-facing documentation that should exist

If the project becomes more widely used, keep these current:

- installation/build instructions
- archive format/storage expectations
- backup and restore guidance
- upgrade/compatibility notes
- security reporting path
- known limitations and sharp edges

## CRA-adjacent engineering checklist

Use this as a practical checklist, not cosplay compliance:

- [ ] known dependencies are documented
- [ ] release notes mention security-relevant fixes
- [ ] there is a private vulnerability reporting path
- [ ] supported behavior and limitations are documented honestly
- [ ] builds/tests are repeatable in CI
- [ ] published artifacts can be tied back to source/version
- [ ] archive/data compatibility expectations are documented
- [ ] significant security bugs get regression coverage

## Current honest status

Today the repo has:

- build/test CI
- documented native dependencies
- a working README with workflows and limitations
- no automated SBOM generation yet
- no formal security SLA or compliance program
- no documented backup/restore procedure yet

That is acceptable at prototype stage. The important part is to be explicit, not pretend the paperwork fairy already visited.
