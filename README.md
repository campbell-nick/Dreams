# Illution (illus)

✦ Dreams Package Manager (illus) v1.0.0 ✦

A lightweight, stateless C package manager that builds and installs software into user space (`~/.local/`). It is sudo-aware when resolving the real user's home directory, but package operations should not be run as root — see [Safe usage](#safe-usage).

## WARNING (Use at your own risk)

This project is still in development. You may face bugs or issues. Please report them if you find any.

## Safe usage

Dreams runs the build system of the project it installs, so treat it like
running `make install` yourself.

- **Do not run `illus --cast --store`, `--update`, `--pull`, or `--shed` as root**
  (including via `sudo` or `doas`). Running `sudo make install` for `illus`
  itself is fine, but the package manager must run as your normal user.
  `--cast --store` and `--update` execute the project's build scripts, while
  `--pull` and `--shed` create or remove files. Files are chowned back to you
  afterward, but that does not undo what a build script already did.
- Ensure `~/.local` and `~/Dreams` are owned by you and are not
  world-writable.
- Run one `illus` command at a time.
- If `--shed` reports that a package was only partially removed, keep its
  manifest in `~/.local/share/illus/` and retry.
- Avoid `--force` on destinations you care about.
- Only install repositories you trust; packages are not sandboxed, signed,
  or verified.

## Known limitations

- Manifest files are written non-atomically; a crash or full disk during an
  install can leave a truncated manifest.
- There is no locking, so concurrent `illus` processes are not supported.
- Filesystem containment checks are best-effort and do not guard against a
  malicious local process racing symlinks inside `~/.local`.
- `~/.local` and the home directory are assumed to be real directories, not
  symlinks.

## Features

- **Stateless design**: Builds from the current working directory and resolves target paths dynamically without a central database.
- **Sudo-aware directory resolution**: Uses `SUDO_USER` and `<pwd.h>` to find the real user's home, so an accidental root invocation still targets the correct user space. Package operations should still be run as your normal user.
- **Ownership fixups**: If a package operation does run as root, installed files are recursively chowned back to the invoking user. This does not make root builds safe.
- **Build system detection**: Auto-detects `Makefile` and `CMakeLists.txt` with prefix injection.
- **Manifest tracking**: Installs are staged with `DESTDIR` and moved into `~/.local` only after a successful build, so manifests record exactly the files the package installed. If a build system ignores `DESTDIR`, illus warns and skips the manifest instead of guessing.
- **Package or binary lookup**: `--shed` accepts either the package/repository name or an installed binary name.

## Requirements

- Linux or another POSIX-like system
- A C99 compiler (GCC or Clang)
- `make` and/or `cmake`, depending on the project being built
- `git` for `--pull` and `--update`
- Standard coreutils (`install`, `mkdir`, `rm`, ...)

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

## Building & Installing `Dreams`

Clone the repo:

```bash
git clone https://github.com/campbell-nick/Dreams.git
```

Build and install `illus` locally to `~/.local/bin`:

```bash
make
make install
```

Build and install `illus` system-wide to `/usr/local/bin`:

```bash
make
sudo make install
```

## Removing `illus`

If you installed it locally:

```bash
make uninstall
```

If you installed it system-wide:

```bash
sudo make uninstall
```

This removes only the `illus` binary. Packages installed with
`--cast --store` must be removed with `illus --shed <package-or-binary>`.

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

## Troubleshooting

- **`illus: command not found`** — add `~/.local/bin` to your `PATH`, for
  example `export PATH="$HOME/.local/bin:$PATH"`.
- **`Package '<name>' is not installed`** — the manifest may be missing.
  `--shed <binary>` falls back to removing `~/.local/bin/<binary>`, or re-run
  `--cast --store` to recreate tracking.
- **`build system ignored DESTDIR; no manifest written`** — the project's
  install rule must respect `$(DESTDIR)$(PREFIX)` (Make) or `DESTDIR` with
  `CMAKE_INSTALL_PREFIX` (CMake). Without that, files are installed but cannot
  be tracked or cleanly shed.
- **`partially removed`** — some files could not be removed (permissions, or
  paths outside `~/.local`). Fix the cause and re-run `--shed`; the manifest
  is kept for retries. Do not delete it manually unless you intend to lose
  track of the remaining files.
- **`destination already exists`** — `--pull` refuses to overwrite an existing
  directory. Use a different `--dir`, or `--force` for destinations inside your
  home directory or `/tmp`/`/var/tmp`.
- **Leftover staging directories** — a crash can leave
  `~/.local/.illus-stage-*`; they are safe to delete.
