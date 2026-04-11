# Archive integrity and recovery

This document defines the **practical MVP expectations** for keeping a Docmasys archive recoverable.

It is intentionally blunt:
- Docmasys currently gives you **immutable versions + CAS deduplication + workspace drift detection**.
- It does **not** yet provide a full built-in archive scrubber / `fsck` / repair tool for archive internals.
- If `content.db` or `Objects/` are lost or corrupted, recovery quality depends on your backups.

## 1. What an archive actually is

A Docmasys archive is a directory containing at least:

- `content.db` — SQLite metadata
- `Objects/` — compressed CAS objects addressed by SHA-256

During normal SQLite WAL mode operation, you may also see:

- `content.db-wal`
- `content.db-shm`

Those sidecar files are normal while the database is open.

## 2. Current integrity model

For the MVP, integrity means four different things:

### Archive metadata integrity
`content.db` must be readable by SQLite and structurally consistent with the current schema.

### Blob-store integrity
Objects under `Objects/` must exist and remain readable/decompressible when a version is materialized.

### Metadata-to-blob consistency
A file version in `content.db` is expected to point at a blob hash that actually exists in `Objects/`.

### Workspace integrity
Tracked workspaces can be checked for drift:
- `ok`
- `missing`
- `modified`
- `replaced`

This is what `Docmasys status` verifies today.

## 3. What Docmasys verifies today

### What is verified by current code/CLI

- Imported content is hashed with SHA-256 before entering CAS.
- New blobs are stored in the archive CAS and tracked in SQLite.
- Readonly tracked workspace files are checked for drift.
- Readonly copies that become writable again are treated as `modified`.
- Readonly symlinks are checked to ensure they still point to the expected CAS blob.
- `repair` can rematerialize damaged or missing **readonly tracked files**.
- `checkin` refuses obvious bad states for checked-out files such as `missing` or `replaced`.
- `inspect` shows current logical files and whether their referenced blob is marked `ready` in metadata.

### What is **not** verified by current code/CLI

- No built-in full archive scan that walks every DB-referenced blob and proves every object file exists.
- No built-in command that decompresses every CAS object and re-hashes it for a full archive audit.
- No built-in repair of broken archive internals if `content.db` or `Objects/` are inconsistent.
- No built-in recovery from a corrupted SQLite database.
- No snapshot/backup orchestration.

So the honest operator position is:

> Docmasys can detect workspace drift and some operational breakage, but archive-internal integrity still depends heavily on sane backups and restore drills.

## 4. Minimum backup set

### Safe rule
Back up the **entire archive directory**, not selected files.

That means at minimum:
- `content.db`
- `Objects/`
- `content.db-wal` if present
- `content.db-shm` if present

If you only back up `content.db` without `Objects/`, the archive metadata survives but file content may be unrecoverable.

If you only back up `Objects/` without `content.db`, the blobs survive but the logical file/version history becomes effectively unusable.

### Cold backup vs hot backup

#### Preferred MVP approach: cold backup
Use a backup flow where no Docmasys process is writing the archive during the copy.

That keeps things simple and avoids guessing whether a live WAL state was captured cleanly.

#### If you copy a live archive
Include the SQLite sidecars when present:
- `content.db-wal`
- `content.db-shm`

Without them, a hot copy may be incomplete.

### Operator recommendation
Until Docmasys grows first-class backup tooling, treat the archive directory as an atomic unit:

- snapshot/copy the whole directory
- keep multiple restore points
- store at least one backup off the machine
- periodically prove a backup restores cleanly

## 5. Practical restore drill

This is the restore drill the MVP should be able to pass.

Assume:
- source backup is at `/backups/docmasys-archive/`
- restore target is `/tmp/docmasys-restore-test/`

### Step 1: restore the archive copy

```bash
rm -rf /tmp/docmasys-restore-test
cp -a /backups/docmasys-archive /tmp/docmasys-restore-test
```

Use your real snapshot/restore method instead of `cp -a` if needed. The point is to create an isolated restored archive copy.

### Step 2: verify SQLite can open cleanly

```bash
sqlite3 /tmp/docmasys-restore-test/content.db "PRAGMA integrity_check;"
```

Expected result:

```text
ok
```

If this does not return `ok`, stop calling the backup healthy.

### Step 3: inspect the restored archive with Docmasys

```bash
Docmasys inspect --archive /tmp/docmasys-restore-test
```

Expectations:
- command succeeds
- expected logical files are listed
- blob column shows `ready` for stored content

Important limitation:
`inspect` does **not** prove every object file is physically present and readable. It only confirms the DB metadata view.

### Step 4: materialize a sample file or set of files

```bash
mkdir -p /tmp/docmasys-restore-ws
Docmasys get \
  --archive /tmp/docmasys-restore-test \
  --ref docs/readme.txt \
  --out /tmp/docmasys-restore-ws \
  --mode readonly-copy
```

Expectations:
- command succeeds
- requested file appears in the workspace
- file content matches expectation
- readonly copy is not writable

If this fails with retrieval/decompression errors, your backup is not healthy enough.

### Step 5: run workspace status on the restored sample workspace

```bash
Docmasys status --archive /tmp/docmasys-restore-test --root /tmp/docmasys-restore-ws
```

Expected result for the sampled file(s):
- `ok`

### Step 6: optional edit-flow proof

For a stronger drill, also prove checkout/checkin still works:

```bash
mkdir -p /tmp/docmasys-restore-edit
Docmasys checkout \
  --archive /tmp/docmasys-restore-test \
  --ref docs/readme.txt \
  --out /tmp/docmasys-restore-edit \
  --user restore-test \
  --environment lab

printf 'restore drill edit\n' > /tmp/docmasys-restore-edit/docs/readme.txt

Docmasys checkin \
  --archive /tmp/docmasys-restore-test \
  --root /tmp/docmasys-restore-edit \
  --ref docs/readme.txt \
  --user restore-test \
  --environment lab
```

This proves more than read-only recovery:
- DB is writable
- CAS can store new content
- lock flow still works
- version history can advance

Use a disposable restored copy for this step, not your production archive.

## 6. Failure signals operators should take seriously

Treat the following as archive/workspace integrity warnings, not cosmetic annoyances.

### Workspace-level failure signals

#### `status` reports `missing`
Tracked file is gone from the workspace.

#### `status` reports `modified`
Tracked file content changed, or a readonly copy became writable again.

#### `status` reports `replaced`
Expected file/symlink shape no longer matches what Docmasys recorded.

#### `import` fails on a managed readonly workspace
Current behavior intentionally rejects tampered readonly tracked files and tells you to use `repair` or explicit checkout/checkin flow.

### Archive-level failure signals

#### `sqlite3 ... PRAGMA integrity_check;` is not `ok`
Assume metadata corruption until proved otherwise.

#### `Docmasys inspect` fails to open the archive
Assume archive metadata or schema compatibility trouble.

#### `Docmasys get`, `repair`, or other materialization fails with CAS retrieval/decompression errors
Likely causes include:
- missing object file under `Objects/`
- truncated/corrupted compressed object
- broken backup/restore

#### Blob metadata says `ready` but materialization fails
That means the DB believes the blob exists, but the actual object store does not satisfy that claim. That is a real integrity problem.

## 7. What `repair` means in MVP terms

`Docmasys repair` is **workspace repair**, not archive reconstruction.

Today it:
- rematerializes readonly tracked files that are missing/modified/replaced
- preserves the recorded version
- skips checked-out files

Today it does **not**:
- repair `content.db`
- rebuild missing CAS objects
- reconcile broken DB-to-object references
- recover deleted versions from partial damage

If archive internals are broken, restore from backup.

## 8. Recommended operating practice for MVP users

For now, the sane operating model is:

1. treat archive directories as backup units
2. prefer cold backups or filesystem snapshots
3. keep multiple backup generations
4. run a restore drill periodically
5. use `status` as workspace drift detection, not as a full archive health certificate
6. use `repair` only for readonly workspace rematerialization
7. if archive internals look broken, restore instead of improvising surgery on production data

## 9. Honest MVP guarantee

Current practical guarantee:

- If you keep a good backup of the full archive directory, Docmasys should be restorable in a straightforward way.
- If you lose either the SQLite metadata or the CAS object store, recovery becomes partial at best.
- Without backups, Docmasys does not yet promise miracle recovery from archive corruption.

That is not glamorous, but it is honest — which is better than shipping fake confidence.