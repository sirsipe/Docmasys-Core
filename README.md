# Docmasys Core

[![CI](https://github.com/sirsipe/Docmasys-Core/actions/workflows/ci.yml/badge.svg)](https://github.com/sirsipe/Docmasys-Core/actions/workflows/ci.yml)

Docmasys Core is a small document/archive engine built around:

- content-addressed storage (CAS)
- SQLite metadata
- immutable file versions
- explicit file relations
- typed per-version properties
- workspace materialization
- checkout/checkin style editing flow

It is designed as a reusable core library plus a standalone CLI.

## Status

This is still a prototype. The main archive/workspace loop now exists, but archive recovery guarantees are still MVP-level and should be read literally:

- tracked workspace drift detection exists
- readonly workspace repair exists
- immutable versions + CAS storage exist
- a full built-in archive scrubber / `fsck` / internal repair tool does **not** exist yet
- backups are therefore part of the product story, not an optional afterthought

See `docs/ARCHIVE_INTEGRITY_AND_RECOVERY.md` for the current operational expectations.

The main archive/workspace loop now exists:

- import files into an archive
- materialize readonly views
- checkout writable copies
- check changes back in as new versions
- detect drift in managed workspaces
- repair readonly materializations
- manage stale locks explicitly

## Build

Requires:

- C++20 compiler
- OpenSSL
- Zstd
- SQLite3
- CMake

```bash
git clone https://github.com/sirsipe/Docmasys-Core
cd Docmasys-Core
cmake -S . -B build
cmake --build build
./build/bin/Docmasys help
```

## CLI overview

```text
Docmasys import    --archive <archive> --root <folder> [--include <glob> | --includes-file <file>]... [--ignore <glob> | --ignores-file <file>]...
Docmasys get       --archive <archive> (--ref <path[@version]> | --refs-file <file>)... [--out <folder>] [--scope none|strong|strong+weak|all] [--mode readonly-copy|readonly-symlink]
Docmasys checkout  --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --out <folder> --user <user> --environment <environment> [--scope none|strong|strong+weak|all]
Docmasys checkin   --archive <archive> (--ref <path> | --refs-file <file>)... --root <folder> --user <user> --environment <environment> [--keep-lock true|false]
Docmasys unlock    --archive <archive> (--ref <path> | --refs-file <file>)...
Docmasys status    --archive <archive> --root <folder>
Docmasys repair    --archive <archive> --root <folder>
Docmasys versions  --archive <archive> (--path <path> | --paths-file <file>)...
Docmasys relate    --archive <archive> [--from <path[@version]> --to <path[@version]> --type strong|weak|optional]... [--edges-file <file>]
Docmasys relations --archive <archive> (--ref <path[@version]> | --refs-file <file>)... [--type strong|weak|optional|all]
Docmasys props list   --archive <archive> (--ref <path[@version]> | --refs-file <file>)...
Docmasys props get    --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property>
Docmasys props set    --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property> --type string|int|bool --value <value>
Docmasys props remove --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property>
Docmasys locks list   --archive <archive>
Docmasys inspect   --archive <archive> [--root <folder>]
```

## Core concepts

### Archive
An archive is a directory containing:

- `content.db` SQLite metadata
- compressed CAS objects under `Objects/`

### Logical file
A stable archive path like:

```text
reports/summary.txt
```

### Version
Each logical file can have many immutable versions:

```text
reports/summary.txt@1
reports/summary.txt@2
```

### Blob
Actual file content is stored once in CAS by hash.

### Materialization
Files can be projected into a workspace as:

- `readonly-copy`
- `readonly-symlink`
- `checkout-copy`

### Checkout lock
A logical file can be marked as checked out by a user/environment/workspace tuple.

## Archive integrity and recovery

### What counts as a complete archive backup

Back up the whole archive directory:

- `content.db`
- `Objects/`
- `content.db-wal` if present
- `content.db-shm` if present

In practice, the safe rule is simple: copy or snapshot the entire archive directory, preferably while nothing is writing to it.

### What Docmasys can verify today

- `status` detects tracked workspace drift: `ok`, `missing`, `modified`, `replaced`
- `repair` rematerializes damaged readonly tracked files
- `inspect` reports current logical files and blob readiness from metadata
- checkout/checkin flow refuses some obviously bad workspace states

### What Docmasys cannot verify today

- no built-in full archive `fsck`
- no built-in command that proves every DB-referenced blob exists and decompresses cleanly
- no built-in repair for corrupted `content.db` or missing CAS objects

### Minimum restore drill

Use a restored copy, not production:

```bash
cp -a /path/to/backup /tmp/docmasys-restore-test
sqlite3 /tmp/docmasys-restore-test/content.db "PRAGMA integrity_check;"
Docmasys inspect --archive /tmp/docmasys-restore-test
Docmasys get --archive /tmp/docmasys-restore-test --ref docs/readme.txt --out /tmp/docmasys-restore-ws --mode readonly-copy
Docmasys status --archive /tmp/docmasys-restore-test --root /tmp/docmasys-restore-ws
```

Expected outcome:
- SQLite integrity check returns `ok`
- `inspect` succeeds and lists expected files
- `get` succeeds
- restored workspace reports `ok`

The full practical guidance lives in `docs/ARCHIVE_INTEGRITY_AND_RECOVERY.md`.

## Architecture

```mermaid
flowchart LR
    CLI[Docmasys CLI] --> Vault[Vault / workspace logic]
    Vault --> DB[(SQLite metadata)]
    Vault --> CAS[(CAS object store)]
    Vault --> EXT[Import extensions]
    DB --> REL[Versions / relations / properties]
    DB --> WS[Workspace tracking / locks]
```

## Archive data model

```mermaid
erDiagram
    FOLDERS ||--o{ FOLDERS : contains
    FOLDERS ||--o{ FILES : contains
    FILES ||--o{ FILE_VERSIONS : has
    FILE_VERSIONS }o--|| BLOBS : uses
    FILE_VERSIONS ||--o{ VERSION_RELATIONS : "from"
    FILE_VERSIONS ||--o{ VERSION_RELATIONS : "to"
    FILE_VERSIONS ||--o{ VERSION_PROPERTIES : has
    FILES ||--o| CHECKOUT_LOCKS : locked_by
    FILES ||--o{ WORKSPACE_ENTRIES : materialized_in
```

## Main workflows

### Import workflow

```mermaid
flowchart TD
    A[Scan root folder] --> B[Hash file]
    B --> C[Insert or reuse blob]
    C --> D[Create new version if content changed]
    D --> E[Store blob in CAS if pending]
    E --> F[Run import extensions]
```

### Readonly materialization workflow

```mermaid
flowchart TD
    A[Resolve ref and version] --> B[Resolve relation closure]
    B --> C{Mode}
    C -->|readonly-copy| D[Retrieve file locally and remove write bits]
    C -->|readonly-symlink| E[Create symlink to CAS blob]
    D --> F[Record workspace entry]
    E --> F
```

### Checkout / edit / checkin workflow

```mermaid
flowchart TD
    A[Checkout request] --> B[Acquire logical-file lock]
    B --> C[Materialize writable copy]
    C --> D[User edits file]
    D --> E[Checkin request]
    E --> F[Verify lock ownership]
    F --> G[Import changed content]
    G --> H[Create new version if needed]
    H --> I[Update workspace entry]
    I --> J[Release lock by default]
```

### Status / repair workflow

```mermaid
flowchart TD
    A[Load tracked workspace entries] --> B[Compare filesystem state to expected state]
    B --> C{State}
    C -->|ok| D[No action]
    C -->|missing modified replaced| E{Readonly or checkout}
    E -->|readonly| F[Repair can rematerialize]
    E -->|checkout| G[Leave for user / checkin flow]
```

## Behavior summary

### `import`
- imports a folder tree into an archive
- can filter imported paths with include/ignore glob rules matched against root-relative paths
- creates new versions only when content changed
- stores new blobs in CAS
- rejects tampered readonly tracked files inside managed workspaces
- rejects readonly copies that have become writable again, even if file contents still match

### `get`
- materializes one or more refs into a workspace
- supports readonly copy and readonly symlink modes
- can include relation closure by scope

### `checkout`
- acquires logical-file lock
- materializes writable copies
- requires explicit `--user` and `--environment`

### `checkin`
- accepts logical paths only
- verifies lock ownership
- imports changed content as new version
- updates workspace tracking
- releases lock unless `--keep-lock true`

### `status`
Reports tracked workspace state:

- `ok`
- `missing`
- `modified`
- `replaced`

### `repair`
- repairs readonly tracked files
- skips checked-out files

### `unlock`
- force clears stale locks
- intentionally blunt

## Batch usage

Batch input is consistent across commands:

- repeat selector flags multiple times
- or use manifest files like `--refs-file`, `--paths-file`
- manifest files ignore blank lines and `#` comments

Example `refs.txt`:

```text
reports/q1.txt@2
notes/todo.txt@1
```

Example `paths.txt`:

```text
reports/q1.txt
notes/todo.txt
```

Example `edges.txt`:

```text
reports/q1.txt@2 notes/todo.txt@1 strong
reports/q1.txt@2 refs/appendix.txt@1 optional
```

## Examples

### Create an archive

```bash
mkdir -p demo-src/docs
printf 'hello\n' > demo-src/docs/readme.txt

Docmasys import --archive ./demo-archive --root ./demo-src
```

### Inspect current logical files

```bash
Docmasys inspect --archive ./demo-archive
```

`inspect` now prints tab-separated columns suitable for quick reports and shell piping:

```text
path    version blob    properties      outgoing_relations
ROOT/docs/readme.txt    1       ready   0       0
```

### List versions

```bash
Docmasys versions --archive ./demo-archive --path docs/readme.txt
```

### Materialize readonly copies

```bash
Docmasys get \
  --archive ./demo-archive \
  --ref docs/readme.txt \
  --out ./workspace \
  --mode readonly-copy
```

### Materialize readonly symlinks

```bash
Docmasys get \
  --archive ./demo-archive \
  --ref docs/readme.txt \
  --out ./workspace \
  --mode readonly-symlink
```

### Add properties

```bash
Docmasys props set \
  --archive ./demo-archive \
  --ref docs/readme.txt@1 \
  --name reviewed \
  --type bool \
  --value true

Docmasys props list --archive ./demo-archive --ref docs/readme.txt@1
```

### Add relations

```bash
Docmasys relate \
  --archive ./demo-archive \
  --from docs/readme.txt@1 \
  --to refs/appendix.txt@1 \
  --type strong

Docmasys relations --archive ./demo-archive --ref docs/readme.txt@1
```

### Checkout and checkin

```bash
Docmasys checkout \
  --archive ./demo-archive \
  --ref docs/readme.txt \
  --out ./workspace \
  --user alice \
  --environment laptop

printf 'changed\n' > ./workspace/docs/readme.txt

Docmasys checkin \
  --archive ./demo-archive \
  --root ./workspace \
  --ref docs/readme.txt \
  --user alice \
  --environment laptop
```

### Status and repair

```bash
Docmasys status --archive ./demo-archive --root ./workspace
Docmasys repair --archive ./demo-archive --root ./workspace
```

### Unlock a stale lock

```bash
Docmasys unlock --archive ./demo-archive --ref docs/readme.txt
```

## Extension system

Docmasys runs import extensions for newly created versions.

Extensions can:

- inspect imported file contents/path
- attach typed properties
- emit version-to-version relations

Built-in examples currently include:

- file facts
- relation manifest parsing for `.dmsrel`

Example `.dmsrel` file:

```text
strong docs/readme.txt@1
optional refs/appendix.txt@1
```

## Design notes

- paths are vault-relative; `ROOT/` is optional in user input
- omitting `@version` means latest
- property names are case-insensitive per version
- relation scopes:
  - `none`
  - `strong`
  - `strong+weak`
  - `all`
- `checkin` and `unlock` accept logical paths, not `@version` selectors
- security/identity enforcement is intentionally outside this core

## Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Continuous integration

GitHub Actions CI is configured in:

```text
.github/workflows/ci.yml
```

Current pipeline:

- runs on push and pull request
- builds on Ubuntu
- installs required native dependencies
- configures with CMake
- builds the project
- runs the full CTest suite

Development process expectations are documented in:

```text
docs/ARCHITECTURE_DEVOPS_POLICY.md
```

## Project docs

Additional lightweight project/process docs live under `docs/`:

- `docs/ARCHIVE_INTEGRITY_AND_RECOVERY.md` - practical backup, restore, and archive integrity expectations for the MVP
- `docs/BACKLOG.md` - work tracking template and current priorities
- `docs/SECURITY.md` - security goals, threat model, and vulnerability reporting path
- `docs/DEPENDENCIES_AND_SBOM.md` - dependency expectations and SBOM guidance
- `docs/RELEASES_AND_COMPLIANCE.md` - release hygiene and CRA-adjacent notes

## Current limitations

- `inspect` is lightweight but now reports version, blob readiness, property count, and outgoing relation count
- there is no built-in full archive integrity scrubber / `fsck` yet; use the backup/restore guidance in `docs/ARCHIVE_INTEGRITY_AND_RECOVERY.md`
- `relations` currently reports outgoing relations only
- batch commands fail fast on first invalid item
- readonly symlink behavior still needs validation on Windows environments
- `unlock` has no admin/permission layer
- include/ignore matching currently supports simple glob patterns (`*`, `?`, `**`) against workspace-relative paths
