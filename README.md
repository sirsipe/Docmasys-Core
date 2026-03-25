# Docmasys Core

Small document vault prototype with:
- content-addressed storage (CAS)
- SQLite metadata
- verb-based CLI
- lightweight file version tracking and relations
- per-version typed properties
- import-time extension hooks
- batch-friendly CLI inputs for scripting and large runs

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
Docmasys import    --archive <archive> --root <folder>
Docmasys get       --archive <archive> (--ref <path[@version]> | --refs-file <file>)... [--out <folder>] [--scope none|strong|strong+weak|all]
Docmasys versions  --archive <archive> (--path <path> | --paths-file <file>)...
Docmasys relate    --archive <archive> [--from <path[@version]> --to <path[@version]> --type strong|weak|optional]... [--edges-file <file>]
Docmasys relations --archive <archive> (--ref <path[@version]> | --refs-file <file>)... [--type strong|weak|optional|all]
Docmasys props list   --archive <archive> (--ref <path[@version]> | --refs-file <file>)...
Docmasys props get    --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property>
Docmasys props set    --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property> --type string|int|bool --value <value>
Docmasys props remove --archive <archive> (--ref <path[@version]> | --refs-file <file>)... --name <property>
Docmasys inspect   --archive <archive> [--root <folder>]
```

### Batch UX

The batch model is intentionally uniform instead of inventing command-specific flags:

- repeat the primary selector flag when you already have values in shell variables
- or pass a manifest file with one selector per line (`--refs-file`, `--paths-file`)
- manifests ignore blank lines and `#` comments
- output stays line-oriented so it is easy to pipe into other tools

For `relate`, batch input works in two ways:

- repeat `--from`, `--to`, and `--type` in matching order
- or use `--edges-file` with one whitespace-separated triple per line: `<from> <to> <type>`

If `relate` receives multiple pairs but only one `--type`, that type is broadcast to all pairs.

### Notes

- Paths are vault-relative. `ROOT/` is optional in user input.
- Version references use `path@version`. Omitting `@version` means “latest”.
- Version property names are case-insensitive unique keys per version.
- Property value types are `string`, `int`, and `bool`.
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

# list versions for many files from a manifest
Docmasys versions --archive ./archive --paths-file ./paths.txt

# fetch the latest version of one file
Docmasys get --archive ./archive --ref reports/q1.txt --out ./restore

# fetch many files in one run
Docmasys get --archive ./archive \
  --ref reports/q1.txt \
  --ref refs/appendix.txt@1 \
  --out ./restore

# fetch a specific version and include strong+weak dependencies
Docmasys get --archive ./archive --ref reports/q1.txt@2 --out ./restore --scope strong+weak

# relate one version to another
Docmasys relate --archive ./archive \
  --from reports/q1.txt@2 \
  --to refs/appendix.txt@1 \
  --type strong

# relate many pairs from a manifest
cat > edges.txt <<'EOF'
reports/q1.txt@2 refs/appendix.txt@1 strong
reports/q1.txt@2 refs/note.txt@2 optional
EOF
Docmasys relate --archive ./archive --edges-file ./edges.txt

# attach typed metadata to one or many versions
Docmasys props set --archive ./archive --ref reports/q1.txt@2 --name reviewed --type bool --value true
Docmasys props set --archive ./archive \
  --refs-file ./refs.txt \
  --name reviewed --type bool --value true
Docmasys props get --archive ./archive --refs-file ./refs.txt --name reviewed
Docmasys props list --archive ./archive --refs-file ./refs.txt
```

## Import extensions

Docmasys now has a small import-extension pipeline. Every newly created file version passes through built-in handlers after its blob is archived. Extensions can:

- inspect the local file contents/path
- attach typed properties to the imported version
- emit version-to-version relations

That is enough groundwork for a future SOLIDWORKS-specific importer that inspects a newly archived CAD file and emits dependency relations.

### Built-in examples

- `file-facts`: stores a couple of basic properties such as `file.extension` and `file.filename`
- `relation-manifest`: parses `.dmsrel` files whose lines look like:

```text
strong parts/widget.sldprt@3
weak docs/spec.pdf@1
optional refs/note.txt@2
```

Each line creates an outgoing relation from the imported `.dmsrel` version to the referenced version.

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
- `.dmsrel` entries require explicit `@version` references and currently resolve only to already imported target versions.
- Batch commands still fail fast on the first invalid item; they do not yet offer partial-success reporting.
- Legacy archives created before version metadata existed may need a fresh import for full history visibility.
