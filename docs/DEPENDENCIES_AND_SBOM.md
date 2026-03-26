# Dependencies and SBOM expectations

This is the practical dependency/SBOM note for Docmasys-Core.

## Current direct build dependencies

From the documented build and CI flow, the project currently expects at least:

- C++20 toolchain
- CMake
- OpenSSL
- SQLite3
- Zstd
- pkg-config (used in CI/build environments)

These are meaningful supply-chain inputs and should be treated as part of the shipped product story, even when binaries are built locally.

## Minimum dependency hygiene

For normal development and releases:

- keep dependency names and minimum expectations visible in `README.md`
- record any newly added native/runtime dependency in this file
- prefer widely packaged, maintained dependencies over obscure extras
- update CI when dependency requirements change
- note when a release depends on unusually old or pinned system packages

## SBOM expectation

A formal SBOM generator is not wired in yet, but releases should still have an explicit dependency record.

Minimum acceptable release-time output for now:

- project version or commit
- build platform
- compiler/toolchain version if known
- direct dependencies used to build
- any bundled or statically linked third-party components

## Preferred future state

Future releases should generate a machine-readable SBOM artifact, ideally one of:

- SPDX
- CycloneDX

That artifact should be attached to the release or committed alongside release materials.

## Why this matters here

Docmasys-Core is about preserving and trusting documents over time. That makes provenance and dependency visibility more relevant than in a random toy CLI. If users are supposed to trust archive behavior, maintainers should be able to say what the binary was built from.
