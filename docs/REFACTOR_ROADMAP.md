# Refactor roadmap

## Goals

Start with structural cleanup that reduces repetition and sharp edges without changing repository behavior.

## Dependency order

1. **#14 shared path/materialization helpers**
   - Lowest risk, immediately useful across `Database`, `Vault`, and CLI parsing.
   - Reduces duplicated `ROOT`-path normalization and workspace-root canonicalization logic.
2. **#11 DB statement helpers + DB internals cleanup**
   - Build on shared helpers to reduce raw `sqlite3_stmt*` lifecycle noise.
   - Safer to do before any ownership/API simplification.
3. **#13 CLI modularization**
   - After path helpers exist, extract parsing/output helpers or command handlers in smaller slices.
4. **#15 internal test utilities**
   - Introduce reusable temp-dir / fixture helpers while preserving current test coverage.
5. **#12 reduce `shared_ptr` overuse**
   - Highest semantic risk. Prefer preparatory cleanup first, then narrow targeted API changes later.

## Planned phases

### Phase 1 - Foundation helpers
- Add shared filesystem/path utilities for:
  - vault-relative path normalization
  - canonical workspace-root strings
  - safe vault-relative path derivation
- Reuse them in `Vault`, `Database`, and CLI.

### Phase 2 - SQLite wrapper cleanup
- Introduce small internal SQLite statement helpers/RAII.
- Replace hand-written prepare/bind/finalize sequences in `Database.cpp` incrementally.
- Keep SQL and behavior unchanged.

### Phase 3 - CLI structure cleanup
- Extract reusable parsing/output helpers from `main.cpp`.
- If still safe after phases 1-2, start moving command bodies into dedicated functions/files.

### Phase 4 - Test support
- Add internal test utilities for temp directories and tiny file helpers.
- Update existing tests to use them instead of duplicating setup code.

### Phase 5 - Ownership simplification prep
- Reassess `shared_ptr` usage after DB and path refactors.
- Prefer converting leaf/internal APIs first, not broad signature churn in one pass.

## Risk notes

- **Low risk:** path helpers, test utilities, SQLite RAII wrappers.
- **Medium risk:** CLI decomposition and DB query extraction.
- **High risk:** changing pointer ownership semantics across DB/domain types.

## Progress after pass 2

Completed in the first two safe passes:
- shared path helper extraction (#14)
- SQLite statement helper introduction and selective DB cleanup (#11)
- initial test utility extraction (#15)
- CLI decomposition into `src/cli` command/support modules (#13)
- DB implementation split into focused translation units for core records, relations, properties, and workspace state (#11)
- broader reuse of `src/tests/TestSupport.hpp`, including CLI smoke tests (#15)

## Next recommended slices

1. **#11 DB query-object cleanup**
   - If more cleanup is wanted, extract repeated row-to-domain mapping behind tiny internal query helpers or repositories.
   - Keep SQL local and behavior-stable; avoid abstracting SQLite into a framework.
2. **#13 CLI command file split by verb groups**
   - Current `src/cli/Commands.cpp` is much smaller than the old `main.cpp`, but can still be split further into archive/workspace/metadata command files if desired.
3. **#12 ownership simplification (targeted only)**
   - Prefer leaf/internal conversions where lookup helpers can return values or IDs without widening churn.
   - Good candidates are temporary traversal/query helpers that do not escape module boundaries.
   - Avoid changing public DB/Vault APIs wholesale until invariants and lifetimes are clearer.
