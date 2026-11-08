# Usage

[Back to README](../README.md)

## Files and paths

| Path | Purpose |
| --- | --- |
| `~/Dreams/<repo>/` | Default clone destination for `install` and `pull` |
| `~/.local/` | Install prefix used by `cast --store` and `install` |
| `~/.local/bin/` | Installed executables |
| `~/.local/share/illus/<Package>.txt` | Manifest: metadata plus one installed path per line |
| `~/.local/.illus-stage-*` | Temporary `DESTDIR` staging directories (removed automatically) |
| `~/Dreams/.illus-clone-*` | Temporary clone directories used by atomic pulls |
| `~/.cache/illus/registry/` | Cached git registry (or `$XDG_CACHE_HOME/illus/registry`) |

`illus` keeps no central database. The package name is the basename of the
directory you run `cast --store` from (or the `--name` value), and the manifest
is named after it. Rebuilding a package re-reads its existing manifest, so files
from the previous version remain tracked.

Set `ILLUS_HOME` to an absolute path to use a different home directory, which
is useful for tests or isolated installs:

```bash
ILLUS_HOME=/tmp/illus-home illus list
```

## Command overview

```text
illus install <name|url>   Clone, build, and install a package
illus remove <name>...     Uninstall packages (aliases: shed, uninstall)
illus list                 List installed packages
illus info <name>          Show installed or registry details
illus search [term]        Search the package registry
illus update               Pull, rebuild, and reinstall ~/Dreams projects
illus pull <url>           Clone a repository
illus cast                 Build the current project
illus doctor               Diagnose common problems
illus version              Show version information
illus help [command]       Show help
```

The original flag form is still supported: `--install`, `--remove`, `--shed`,
`--uninstall`, `--list`, `--info`, `--search`, `--update`, `--pull`, `--cast`,
`--doctor`, `--version`, and `--help`.

Usage errors (unknown options, missing values, wrong argument counts) exit
with status `2` and print the command's usage line.

## Installing packages

```bash
illus install <name>
illus install <url>
illus install <url> --name <package>
```

`install` resolves a name through the registry, clones the repository into
`~/Dreams/<name>` (or `--dir`), then builds and installs it with `DESTDIR`
staging. If the destination already exists and is a git clone, `install`
updates it with `git pull --ff-only` and rebuilds; this is the normal upgrade
path. Use `--reinstall` for a clean rebuild (`--fresh`), or `--force` to
replace the clone entirely.

Useful options:

| Option | Meaning |
| --- | --- |
| `--branch <ref>` | Clone or check out a branch, tag, or commit |
| `--name <pkg>` | Override the package/manifest name |
| `--dir <path>` | Exact clone destination |
| `--force` | Replace an existing destination |
| `--reinstall` | Clean rebuild after updating |
| `--build-system make\|cmake` | Force a build system |
| `--jobs <n>` | Parallel build jobs |
| `--allow-untracked` | Accept an install that ignores `DESTDIR` |
| `--registry <path\|url>` | Registry file, directory, or git URL |
| `--refresh` | Refresh the cached registry |
| `--dry-run` | Show what would happen without changing anything |

Install runs the project's build scripts. Only install packages you trust.

## The registry

The registry is a tab-separated file named `packages.tsv` in the `registry/`
directory of a git repository. The default source is this repository:

```text
https://github.com/campbell-nick/Dreams.git
```

Format:

```text
# name<TAB>url<TAB>branch<TAB>description
hello   https://github.com/example/hello.git   main   Example package
```

Blank lines and lines starting with `#` are ignored. `branch` may be empty to
use the repository default. Override the registry with `--registry` or
`ILLUS_REGISTRY`:

```bash
ILLUS_REGISTRY=/path/to/packages.tsv illus search
ILLUS_REGISTRY=/path/to/registry-repo illus install hello
ILLUS_REGISTRY=https://github.com/example/registry.git illus install hello
```

A local file or directory is used in place. A git URL is cloned into
`~/.cache/illus/registry` and refreshed automatically after 24 hours, or
immediately with `--refresh`. Registry entries are not verified: treat the
registry like any other source of executable code.

```bash
illus search            # list everything
illus search json       # substring match on name, URL, or description
illus info <name>       # installed details, or registry details
```

## Building the project directly

```bash
illus cast
```

`cast` builds the project in the current directory and does **not** install by
default.

```bash
illus cast --store
```

`--store` builds and installs into `~/.local`, then writes a manifest under
`~/.local/share/illus/`. Installation is staged with `DESTDIR`, so only files
created by the package are tracked. A build system that ignores `DESTDIR` is
refused by default; pass `--allow-untracked` to install it without a manifest.

```bash
illus cast --fresh
```

`--fresh` removes the CMake `build/` directory and runs `make clean` (when a
Makefile is present) before building.

## Updating

```bash
illus update
illus update --dry-run
```

Pulls, rebuilds, and reinstalls every repository under `~/Dreams` that has a
recognized build system. Repositories without a `Makefile` or `CMakeLists.txt`
are pulled but not built. `update` runs each project's build scripts, so only
keep repositories you trust in `~/Dreams`. A separate `illus upgrade <name>`
command may be added later; for now use `illus install <name>` to upgrade one
package.

## Listing and inspecting packages

```bash
illus list
illus list --all
illus list --verbose
illus list --json
illus list --verify
```

`list` prints tracked packages sorted by name, with file counts, sizes, and
status (`ok`, `N missing`, or `N unsafe`). `--all` also lists untracked
binaries in `~/.local/bin`. `--verify` exits non-zero when tracked files are
missing or a manifest contains unsafe paths.

```bash
illus info <name>
illus info <name> --files
illus info <name> --json
```

`info` accepts a package name or an installed binary name. If the package is
not installed, it falls back to the registry. `--files` lists every tracked
path.

## Removing packages

```bash
illus remove <name>
illus remove <name> --dry-run
```

Removes every file tracked in the package manifest, then deletes the manifest.
For safety, every path is resolved and must stay inside `~/.local`; paths
outside that directory are refused. Entries that no longer exist are treated as
already removed, so a partially cleaned package can still be finalized. A
manifest containing unsafe entries is refused as a whole.

If no manifest exists, `remove` accepts an installed binary name and removes
`~/.local/bin/<binary>` as a fallback.

## Manifests

Manifests are written atomically (temporary file, `fsync`, `rename`) and use a
small metadata header:

```text
# illus-manifest-v2
# package: hello
# source: https://github.com/example/hello.git
# branch: main
# installed: 2026-01-01T00:00:00Z
bin/hello
share/man/man1/hello.1
```

Paths are relative to `~/.local`. Absolute entries are still accepted for
backward compatibility with manifests written by older versions, but they must
resolve inside `~/.local` or they are counted as unsafe and ignored.

## Dry runs

`install`, `remove`, `update`, and `pull` accept `--dry-run`. A dry run prints
the planned actions and does not clone, build, install, remove, or write
manifests. Read-only commands (`list`, `info`, `search`, `doctor`) ignore it.

## Diagnostics

```bash
illus doctor
illus doctor --json
illus doctor --verbose
```

`doctor` checks the home directory, root status, `~/.local` ownership and
permissions, `PATH`, required tools (`git`, `make`, `cmake`), manifest
integrity, unsafe entries, stale staging directories, untracked binaries, and
the registry configuration. It exits non-zero only when a check fails;
warnings do not change the exit status.

## Environment variables

| Variable | Purpose |
| --- | --- |
| `ILLUS_HOME` | Use a different home directory (tests, isolated installs) |
| `ILLUS_REGISTRY` | Registry file, directory, or git URL |
| `ILLUS_ALLOW_ROOT` | Set to `1` to allow mutating commands as root |
| `NO_COLOR` | Disable ANSI colour output |

## Exit status

Commands generally return `0` on success and non-zero on failure. Usage errors
return `2`. `remove` returns non-zero when a package could not be fully removed
and keeps its manifest for a retry. `list --verify` and `doctor` return
non-zero when they find a real problem.

## See also

- [Warnings and safety](Warnings.md)
- [Troubleshooting](Troubleshooting.md)
