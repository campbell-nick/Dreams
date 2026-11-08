# Usage

[Back to README](../README.md)

## Files and paths

| Path | Purpose |
| --- | --- |
| `~/Dreams/<repo>/` | Default source destination for `install` and `pull` |
| `~/.local/` | Install prefix used by `cast --store` and `install` |
| `~/.local/bin/` | Installed executables |
| `~/.local/share/illus/<Package>.txt` | Manifest: metadata plus one installed path per line |
| `~/.local/.illus-stage-*` | Temporary `DESTDIR` staging directories (removed automatically) |
| `~/Dreams/.illus-clone-*` | Temporary clone/download directories used by atomic fetches |

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
illus install <url>        Fetch, build, and install a package
illus remove <name>...     Uninstall packages (aliases: shed, uninstall)
illus list                 List installed packages
illus info <name>          Show details for an installed package
illus update               Pull, rebuild, and reinstall ~/Dreams projects
illus pull <url>           Clone a git repository
illus cast                 Build the current project
illus doctor               Diagnose common problems
illus version              Show version information
illus help [command]       Show help
```

The original flag form is still supported: `--install`, `--remove`, `--shed`,
`--uninstall`, `--list`, `--info`, `--update`, `--pull`, `--cast`,
`--doctor`, `--version`, and `--help`.

Usage errors (unknown options, missing values, wrong argument counts) exit
with status `2` and print the command's usage line.

## Installing packages

```bash
illus install <url>
illus install <url> --name <package>
illus install <url> --branch <ref>
illus install <tarball-url> --name <package> --sha256 <hex>
```

`install` fetches the source into `~/Dreams/<name>` (or `--dir`), builds it,
and installs it with `DESTDIR` staging.

- **Git sources**: if the destination already exists and is a git clone,
  `install` updates it (`git pull --ff-only`, or checks out the requested
  branch/tag) and rebuilds. This is the normal upgrade path.
- **Tarball sources**: the archive is downloaded, its SHA-256 is verified, and
  it is extracted. Tarball URLs require `--sha256`. Reinstalling a tarball
  refreshes the source from the verified archive.

Use `--reinstall` for a clean rebuild (`--fresh`), or `--force` to replace the
destination entirely.

Useful options:

| Option | Meaning |
| --- | --- |
| `--branch <ref>` | Clone or check out a branch, tag, or commit (git sources) |
| `--name <pkg>` | Override the package/manifest name |
| `--dir <path>` | Exact source destination |
| `--force` | Replace an existing destination |
| `--reinstall` | Clean rebuild after updating |
| `--build-system auto\|make\|cmake\|autotools` | Force a build system |
| `--jobs <n>` | Parallel build jobs |
| `--configure-args <args>` | Extra arguments for `./configure` (Autotools) |
| `--make-args <args>` | Extra arguments for `make` |
| `--sha256 <hex>` | SHA-256 for a tarball URL install |
| `--allow-untracked` | Accept an install that ignores `DESTDIR` |
| `--dry-run` | Show what would happen without changing anything |

`install` runs the project's build scripts. Only install packages you trust.


## Building the project directly

```bash
illus cast
illus cast --store
illus cast --store --build-system autotools --configure-args "--with-feature"
```

`cast` builds the project in the current directory and does **not** install by
default. It auto-detects Make, CMake, and Autotools (CMake wins when both
CMake and Autotools files exist). Autotools runs `autoreconf -fi` when
`configure` is missing, then configures with `--prefix=$HOME/.local`.

`--store` builds and installs into `~/.local`, then writes a manifest under
`~/.local/share/illus/`. Installation is staged with `DESTDIR`, so only files
created by the package are tracked. A build system that ignores `DESTDIR` is
refused by default; pass `--allow-untracked` to install it without a manifest.

`--fresh` removes the CMake `build/` directory, runs `make distclean` for
Autotools when a Makefile exists, or runs `make clean` for plain Make projects
before building.

## Updating

```bash
illus update
illus update --dry-run
```

Pulls, rebuilds, and reinstalls every repository under `~/Dreams` that has a
recognized build system. Repositories without a recognized build system are
pulled but not built. `update` runs each project's build scripts, so only keep
repositories you trust in `~/Dreams`. To upgrade one package, run
`illus install <url>` again (or use `--force`/`--reinstall` as needed).

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

`info` accepts a package name or an installed binary name and shows manifest
details for installed packages only. `--files` lists every tracked path.

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
manifests. Read-only commands (`list`, `info`, `doctor`) ignore it.

## Diagnostics

```bash
illus doctor
illus doctor --json
illus doctor --verbose
```

`doctor` checks the home directory, root status, `~/.local` ownership and
permissions, `PATH`, required tools (`git`, `make`, `cmake`, `autoreconf`,
`tar`), manifest integrity, unsafe entries, stale staging directories, and
untracked binaries. It exits non-zero only when a check fails; warnings do not
change the exit status.

## Environment variables

| Variable | Purpose |
| --- | --- |
| `ILLUS_HOME` | Use a different home directory (tests, isolated installs) |

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
