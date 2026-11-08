# Usage

[Back to README](../README.md)

## Files and paths

| Path | Purpose |
| --- | --- |
| `~/Dreams/<repo>/` | Default destination for `--pull` |
| `~/.local/` | Install prefix used by `--cast --store` |
| `~/.local/bin/` | Installed executables |
| `~/.local/share/illus/<Package>.txt` | Manifest: one installed absolute path per line |
| `~/.local/.illus-stage-*` | Temporary `DESTDIR` staging directories (removed automatically) |

`illus` keeps no central database. The package name is the basename of the
directory you run `--cast --store` from, and the manifest is named after it.
If that name is not a valid single path component, or is too long to form
`<name>.txt`, the package is installed without a manifest and a warning is
shown. Rebuilding a package re-reads its existing manifest, so files from the
previous version remain tracked.

## Basic Usage

### Cloning repositories

```bash
illus --pull <url>
```

Repositories are cloned into `~/Dreams/<repo-name>` by default. `--pull` only
clones; it does not build or install anything. Run `--cast --store` inside the
repository afterward.

To clone into an explicit destination, use `--dir`. The path is the exact destination directory and supports `~`, `~/...`, absolute, and relative paths:

```bash
illus --pull <url> --dir ~/Projects/my-project
```

Existing destinations are refused unless `--force` is passed. `--force` replaces the destination only after its path has been validated; it never follows the final symlink. Recursive replacement is limited to destinations inside your home directory or `/tmp`/`/var/tmp`, and system locations are refused.

### Building the project

```bash
illus --cast
```

`--cast` builds the project in the current directory. It does **not** install by default.

```bash
illus --cast --store
```

`--store` builds and installs into `~/.local`, then writes a manifest under `~/.local/share/illus/`. Installation is staged with `DESTDIR`, so only files created by the package are tracked. A build system that ignores `DESTDIR` still installs, but no manifest is written and a warning is shown.

```bash
illus --cast --fresh
```

`--fresh` removes the CMake `build/` directory and runs `make clean` (when a Makefile is present) before building.

### Updating

```bash
illus --update
```

Pulls, rebuilds, and reinstalls every repository under `~/Dreams` that has a
recognized build system. Repositories without a `Makefile` or
`CMakeLists.txt` are pulled but not built. `--update` runs each project's
build scripts, so only keep repositories you trust in `~/Dreams`.

### Listing packages

```bash
illus --list
```

Lists tracked packages. If no manifests exist yet, it falls back to listing binaries in `~/.local/bin`.

### Uninstalling

```bash
illus --shed <package-or-binary>
```

Removes every file tracked in the package manifest, then deletes the manifest. For safety, every path is resolved and must stay inside `~/.local`; paths outside that directory are refused. Entries that no longer exist are treated as already removed, so a partially cleaned package can still be finalized.

### Other commands

```bash
illus --version
illus --help
```

## Exit status

Commands generally return `0` on success and non-zero on failure. `--shed`
returns non-zero when a package could not be fully removed, and keeps its
manifest so the removal can be retried.

## See also

- [Warnings and safety](Warnings.md)
- [Troubleshooting](Troubleshooting.md)
