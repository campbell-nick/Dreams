# Illution (illus)

✦ Dreams Package Manager (illus) v1.0.0 ✦

A lightweight, stateless C package manager that builds and installs software into user space (`~/.local/`), with support for elevated execution via `sudo`.

## WARNING (Use at your own risk)

This project is still in development. You may face bugs or issues. Please report them if you find any.

## Features

- **Stateless design**: Builds from the current working directory and resolves target paths dynamically without a central database.
- **Sudo-aware directory resolution**: Uses `SUDO_USER` and `<pwd.h>` to find the real user's home, so packages still install into the correct user space under `sudo`.
- **Ownership fixups**: Files installed under `sudo` are recursively chowned back to the invoking user.
- **Build system detection**: Auto-detects `Makefile` and `CMakeLists.txt` with prefix injection.
- **Manifest tracking**: Installs are staged with `DESTDIR` and moved into `~/.local` only after a successful build, so manifests record exactly the files the package installed. If a build system ignores `DESTDIR`, illus warns and skips the manifest instead of guessing.
- **Package or binary lookup**: `--shed` accepts either the package/repository name or an installed binary name.

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

## Removing

```bash
sudo make uninstall
```

## Basic Usage

### Cloning repositories

```bash
illus --pull <url>
```

Repositories are cloned into `~/Dreams/<repo-name>` by default.

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

Pulls, rebuilds, and reinstalls every repository under `~/Dreams` that has a recognized build system.

### Listing packages

```bash
illus --list
```

Lists tracked packages. If no manifests exist yet, it falls back to listing binaries in `~/.local/bin`.

### Uninstalling

```bash
illus --shed <package-or-binary>
```

Removes every file tracked in the package manifest, then deletes the manifest. For safety, every path is resolved and must stay inside `~/.local`; paths outside that directory are refused.

### Other commands

```bash
illus --version
illus --help
```
