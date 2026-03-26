# Architecture & DevOps Policy

This document defines the practical working rules for Docmasys Core.

It is intentionally lightweight: enough structure to keep the project sane, not enough to turn a small C++ repository into paperwork cosplay.

## 1. Branching strategy

### Default branch
- `main` is the only long-lived branch.
- `main` must always be releasable: buildable, testable, and not knowingly broken.

### Working branches
All implementation work happens on short-lived branches created from `main`.

Recommended naming:
- `feat/<area>-<short-description>`
- `fix/<area>-<short-description>`
- `refactor/<area>-<short-description>`
- `docs/<short-description>`
- `chore/<short-description>`
- `release/<version>` only when a coordinated release-prep branch is genuinely needed

Examples:
- `feat/vault-managed-workspaces`
- `fix/cli-checkin-lock-validation`
- `refactor/db-schema-migration-flow`
- `docs/architecture-devops-policy`

Rules:
- one concern per branch
- keep branches short-lived
- rebase on `main` before merge if the branch drifts
- do not continue stacking unrelated work on old branches

## 2. Merge strategy

- Pull requests merge into `main`.
- Prefer **squash merge** for most feature/fix branches to keep `main` readable.
- Use a regular merge commit only when preserving branch structure/history is useful.
- Avoid rebasing already-shared `main` history.

Before merge:
- CI must pass
- obvious review comments must be resolved
- branch should be up to date with `main` if there is conflict risk

## 3. Release and hotfix flow

### Release tagging
- Tag releases directly from `main`.
- Use annotated semantic version tags:
  - `v0.1.0`
  - `v0.1.1`
  - `v0.2.0`
- Create a tag only from a commit that passed CI.

Versioning guidance:
- `v0.x.y` while the project is still prototype/early stabilization
- bump minor version for new user-visible features or workflow changes
- bump patch version for bug fixes, docs-only packaging fixes, or CI-safe corrections

### Hotfixes
- For an unreleased problem: fix on `fix/...` from current `main`, merge normally.
- For a released breakage: branch from the release tag or current `main` as `hotfix/<short-description>`.
- Keep hotfix scope narrow.
- Merge hotfix back to `main`, then tag the patch release.

## 4. Commit message convention

Use short, direct, imperative commit messages.

Preferred format:
- `feat: add checkout lock validation`
- `fix: reject tampered readonly workspace files`
- `refactor: split vault materialization helpers`
- `test: cover readonly symlink tracking`
- `docs: document release tagging flow`
- `ci: run CTest on pull requests`
- `chore: ignore local build directories`

Rules:
- subject line ideally <= 72 chars
- first line should explain what changed, not tell a life story
- add a body when the why matters more than the what
- avoid vague messages like `update stuff` or `fixes`

## 5. Pull request expectations

Each PR should:
- have a clear purpose and narrow scope
- explain user-visible behavior changes
- mention schema, archive-format, CLI, or compatibility impact if any
- include or update tests for behavioral changes
- update docs/README when CLI behavior or workflows change

PR checklist:
- builds locally
- tests pass locally
- CI passes
- no accidental debug output, temporary files, or generated junk
- no unrelated formatting churn

## 6. Architecture ownership

Architecture ownership for this project includes both code-shape decisions and delivery hygiene.

The architect is responsible for:
- keeping the core boundaries clear: CAS, DB, vault/workspace logic, CLI
- watching for archive-format and schema compatibility risks
- preventing the CLI from becoming a thin crust over tangled internals
- ensuring new features come with tests and docs proportional to their impact
- deciding when a change is prototype-level acceptable versus needing hardening

## 7. DevOps ownership

For Docmasys Core, architecture ownership also includes practical DevOps stewardship.

### Build pipeline expectations
The baseline CI pipeline must:
- configure with CMake on a clean machine
- build the project in Release mode
- run the full CTest suite
- fail on any test failure

As the project matures, the next CI additions should be:
1. a matrix for at least Linux + one additional compiler/toolchain path
2. a debug build job
3. warning hygiene improvements
4. packaging/release artifact creation from tags

### Release artifacts
For tagged releases, expected artifacts should eventually include:
- source tarball/zip from GitHub tag
- compiled CLI binary for at least Linux x86_64
- release notes summarizing user-visible changes, schema changes, and known limits

Until packaging is automated, manual releases are acceptable if they are reproducible and documented.

### CI checks
Minimum required CI checks:
- checkout
- dependency install
- CMake configure
- build
- CTest execution

Recommended next checks:
- formatting or lint gate once a formatter policy exists
- sanitizers in a non-release build
- coverage reporting if it helps decision-making rather than vanity
- smoke test that confirms `Docmasys help` and basic archive flow still work in CI

### Dependency and update hygiene
- keep CMake minimum and dependency requirements explicit
- pin important CI actions to major versions already reviewed
- review native dependency changes for portability impact
- update GoogleTest and GitHub Actions dependencies deliberately, not randomly
- treat schema changes and archive-layout changes as compatibility events that require notes and tests

### Environment consistency
- local development should work with the documented dependency set from README
- CI is the reference environment, not tribal knowledge
- avoid hidden machine-specific assumptions in paths, compilers, or shell behavior
- keep build output out of version control (`build/`, `build-*/`, editor state)
- when adding platform-specific behavior, document what is guaranteed and what is best-effort

## 8. Definition of done for non-trivial changes

A non-trivial change is done when:
- code is merged to `main` from a dedicated branch
- tests cover the new behavior or regression
- README/docs reflect changed usage if needed
- CI passes
- the change is explainable in release notes without reverse engineering the diff

## 9. Current repo-specific notes

Based on the current repository state:
- `main` is the right trunk branch
- CI already covers build + test on Ubuntu and should remain mandatory
- there are no release tags yet, so adopting annotated `v0.x.y` tags now is clean
- lingering local topic branches should not become semi-permanent parallel universes
- prototype status is fine, but `main` should still be kept shippable
