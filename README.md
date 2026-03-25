# Docmasys Core

Small document vault prototype with:
- content-addressed storage (CAS)
- SQLite metadata
- verb-based CLI
- lightweight file version tracking and relations

## Build

Requires OpenSSL, Zstd, SQLite3, and a C++20 compiler.

```bash
git clone https://github.com/sirsipe/Docmasys-Core
cd Docmasys-Core
cmake -S . -B build
cmake --build build
./build/bin/Docmasys help
```

## CLI

The CLI now prefers verbs over the old flag soup.

```text
Docmasys import   --archive <archive> --root <folder>
Docmasys get      --archive <archive> --ref <path[@version]> [--out <folder>] [--scope none|strong|strong+weak|all]
Docmasys versions --archive <archive> --path <path>
Docmasys relate   --archive <archive> --from <path[@version]> --to <path[@version]> --type strong|weak|optional
Docmasys relations --archive <archive> --ref <path[@version]> [--type strong|weak|optional|all]
Docmasys inspect  --archive <archive> [--root <folder>]
```

### Notes

- Paths are vault-relative. `ROOT/` is optional in user input.
- Version references use `path@version`. Omitting `@version` means “latest”.
- Retrieval scopes:
  - `none` = only the requested file
  - `strong` = requested file + strong relations
  - `strong+weak` = requested file + strong and weak relations
  - `all` = requested file + strong, weak, and optional relations

## Examples

```bash
# import a working folder into an archive
Docmasys import --archive ./archive --root ./docs

# inspect latest known files
Docmasys inspect --archive ./archive --root ./docs

# list versions for one logical file
Docmasys versions --archive ./archive --path reports/q1.txt

# fetch the latest version of one file
Docmasys get --archive ./archive --ref reports/q1.txt --out ./restore

# fetch a specific version and include strong+weak dependencies
Docmasys get --archive ./archive --ref reports/q1.txt@2 --out ./restore --scope strong+weak

# relate one version to another
Docmasys relate --archive ./archive \
  --from reports/q1.txt@2 \
  --to refs/appendix.txt@1 \
  --type strong

# list outgoing relations from a version
Docmasys relations --archive ./archive --ref reports/q1.txt@2
```

## Test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Current limitations

This is still a prototype.

- `inspect` is intentionally lightweight.
- `relations` currently reports outgoing relations from the chosen version.
- Legacy archives created before version metadata existed may need a fresh import for full history visibility.
