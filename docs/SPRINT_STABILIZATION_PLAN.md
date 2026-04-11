# Sprint stabilization plan

Scope: stabilize and simplify the current base before new features.

Relevant issues:
- #11 DB layer split + SQLite statement helpers
- #12 reduce `shared_ptr` overuse
- #14 shared filesystem/materialization path handling
- new issue: trim premature DB migration logic while keeping a minimal future migration framework

## Current repo state

The repository is already part-way through the intended cleanup:
- DB implementation has been split into focused files:
  - `src/DB/Database.cpp`
  - `src/DB/DatabaseRecords.cpp`
  - `src/DB/DatabaseRelations.cpp`
  - `src/DB/DatabaseProperties.cpp`
  - `src/DB/DatabaseWorkspace.cpp`
  - `src/DB/DatabaseInternal.hpp`
  - `src/DB/SqliteHelpers.hpp`
- Shared path helpers already exist in:
  - `src/Common/PathUtils.hpp`
  - `src/Common/PathUtils.cpp`
- CLI extraction is already in progress:
  - `src/cli/CommandHelpers.*`
  - `src/cli/Commands.*`
- Tests already cover schema versioning, legacy migration, relations, workspace state, checkout/checkin, and materialization.

## Files/areas to touch

### #11 DB refactor / SQLite helpers
Primary:
- `src/DB/Database.hpp`
- `src/DB/DatabaseInternal.hpp`
- `src/DB/SqliteHelpers.hpp`
- `src/DB/Database.cpp`
- `src/DB/DatabaseRecords.cpp`
- `src/DB/DatabaseRelations.cpp`
- `src/DB/DatabaseProperties.cpp`
- `src/DB/DatabaseWorkspace.cpp`

Secondary/tests:
- `src/tests/DB_tests.cpp`
- `CMakeLists.txt`

### #12 reduce shared_ptr overuse
Primary:
- `src/DB/DB_Schema.h`
- `src/DB/Database.hpp`
- `src/DB/DatabaseInternal.hpp`
- `src/DB/DatabaseRecords.cpp`
- `src/DB/DatabaseRelations.cpp`
- `src/DB/DatabaseWorkspace.cpp`
- `src/Vault.hpp`
- `src/Vault.cpp`
- `src/Extensions/Extension.hpp`
- `src/Extensions/Extension.cpp`
- `src/cli/Commands.cpp`

Secondary/tests:
- `src/tests/DB_tests.cpp`
- `src/tests/Vault_tests.cpp`

### #14 shared path/materialization handling
Primary:
- `src/Common/PathUtils.hpp`
- `src/Common/PathUtils.cpp`
- `src/Vault.cpp`
- `src/DB/Database.cpp`
- `src/DB/DatabaseWorkspace.cpp`
- `src/DB/DatabaseInternal.hpp`
- `src/cli/Commands.cpp`

Secondary/tests:
- `src/tests/Vault_tests.cpp`
- `src/tests/CLI_smoke.cpp`

### migration-policy cleanup
Primary:
- `src/DB/Database.cpp`
- `src/DB/DB_Schema.h`
- `src/tests/DB_tests.cpp`
- optional new internal migration helper/header if introduced

## Recommended work order

1. **Finish #14 boundaries first**
   - Keep all vault/workspace path normalization in `src/Common/PathUtils.*`.
   - Extend helpers only where duplication still exists.
   - Goal: `Vault`, DB workspace tracking, and CLI all use the same path semantics.

2. **Finish #11 as an internal cleanup, not a framework rewrite**
   - Keep the current split translation-unit structure.
   - Strengthen `SqliteHelpers.hpp` just enough to remove repetitive prepare/bind/step error handling.
   - Keep SQL near each behavior file; do not introduce a heavy repository/query abstraction.

3. **Do migration-policy cleanup before #12**
   - Simplify schema initialization/migration rules while the DB surface is still stable.
   - Remove speculative migration branches and keep one minimal explicit upgrade path if needed.

4. **Then do #12 in small slices**
   - First convert internal-only traversal/query helpers away from `shared_ptr`.
   - Leave public behavior stable until internal lifetimes are obvious.
   - Only after internals settle, decide whether public DB/Vault APIs should return values, IDs, `optional<T>`, or references.

5. **End with test tightening and any small CLI fallout**
   - Add/adjust tests for path semantics, workspace canonicalization, and migration policy.

## Practical boundaries

### DB layer (#11)
Keep these responsibilities separated:
- `Database.cpp`: connection setup, pragmas, schema bootstrap/migration entrypoints, transactions
- `DatabaseRecords.cpp`: blobs/folders/files/file_versions CRUD + import path
- `DatabaseRelations.cpp`: version graph traversal and relation writes
- `DatabaseProperties.cpp`: typed metadata/property logic
- `DatabaseWorkspace.cpp`: workspace entries, checkout locks, status/repair support
- `DatabaseInternal.hpp`: row mapping helpers and tiny internal helpers only
- `SqliteHelpers.hpp`: statement RAII + tiny bind/step helpers only

Do **not** let `DatabaseInternal.hpp` turn into a second giant implementation dump.

### Path/materialization handling (#14)
Centralize these behaviors:
- ensure `ROOT/...` logical path normalization
- derive vault-relative logical paths from workspace files
- canonicalize workspace-root identity for DB keys
- possibly add one helper for converting logical path -> local workspace output path

Keep file permission and actual materialization side effects in `Vault.cpp`.
Keep CAS blob-path calculation in `CAS`, not path utils.

### Ownership cleanup (#12)
Likely safe early candidates:
- internal folder/file/version row readers returning value objects
- `VersionRelationView` and `MaterializedFile` reshaping to reduce heap allocation churn
- DB lookup helpers that can use IDs or value types internally

Higher-risk / defer until later:
- changing extension-facing `ImportedVersionContext`
- changing many public DB methods at once
- changing recursive materialization and checkout flows together with DB ownership changes

## Migration-framework recommendation

### What to remove now
Remove or collapse **premature** migration behavior that assumes a long-lived migration ladder is already needed.

In practice, that means:
- stop carrying multiple speculative `if (version < N)` branches once they are only supporting in-flight local development history
- avoid embedding future migration structure before there are real released schema versions in the wild
- strongly consider removing `MigrateLegacySchemaIfNeeded()` once the team decides whether the legacy `files.blob_id` schema still needs support

### What minimal framework to keep
Keep only this:
1. `DB_SCHEMA_VERSION`
2. a single current schema definition (`DB_SCHEMA`)
3. one bootstrap path for fresh DB creation
4. at most **one** explicit upgrade path from the oldest format you still promise to open
5. a single place that reads `PRAGMA user_version` and dispatches:
   - `0` or empty DB -> create current schema
   - supported older version -> run minimal explicit upgrader(s)
   - unsupported older/unknown future version -> fail loudly

Recommended policy:
- If there has been **no public release** depending on the legacy schema, delete legacy migration support now and simplify tests accordingly.
- If there **has been** a released schema in use, keep exactly one upgrade path from that release to current, and document that as the supported floor.

### Concrete shape
Prefer replacing the current open-coded mix in `Database.cpp` with something like:
- `InitializeSchemaIfNeeded()`
- `UpgradeSchemaFromV1ToCurrent()` (only if truly needed)
- `RequireSupportedSchemaVersion(version)`

That gives a minimal framework without pretending you need a full migration engine yet.

## Risk notes

### Low risk
- path helper completion
- more SQLite statement helper coverage
- tests around path normalization / workspace canonicalization

### Medium risk
- row-mapper cleanup in `DatabaseInternal.hpp`
- tightening schema bootstrap/migration logic
- splitting residual DB helper code further

### High risk
- broad replacement of `shared_ptr` in public DB and extension APIs
- changing `MaterializedFile`, `WorkspaceEntry`, `CheckoutLock`, and `VersionRelationView` all in one pass
- coupling ownership refactor with migration cleanup in the same commit

## Commit recommendation

Use narrow commits and reference issue numbers in every relevant commit.

Suggested style:
- `#14 Centralize vault/workspace path normalization`
- `#11 Add SQLite statement helper coverage for workspace queries`
- `#11 Split remaining DB row-mapping helpers out of Database.cpp`
- `#12 Replace internal DB shared_ptr traversal with value/ID-based helpers`
- `#16 Remove speculative schema migration ladder; keep minimal schema bootstrap` (replace `#16` with the real new issue number)

If one commit truly spans two issues, reference both in the subject or body, for example:
- `#11 #14 Reuse shared path helpers in DB workspace queries`

Preferred rule:
- one commit = one main issue when possible
- mention secondary issue numbers in the body
- avoid “misc cleanup” commits with no issue reference
