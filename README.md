# Illution (illus)

✦ Dreams Package Manager (illus) v1.1.0 ✦

[![CI](https://github.com/campbell-nick/Dreams/actions/workflows/ci.yml/badge.svg)](https://github.com/campbell-nick/Dreams/actions/workflows/ci.yml)

A lightweight, stateless C package manager that builds and installs software
into user space (`~/.local/`). It can clone a repository, build it with Make or
CMake, install it with `DESTDIR` staging, and track every installed file in a
manifest. A git-backed registry maps package names to repository URLs so you do
not have to paste URLs.

> **Warning:** This project is still in development. You may face bugs or
> issues. Please report them if you find any. Read
> [Warnings and safety](docs/Warnings.md) before installing anything.

## Features

- **Name-based installs**: `illus install <name>` resolves the repository from
  a git-backed registry; URLs and local paths work too.
- **Stateless manifests**: The package name comes from the directory you build,
  and manifests are stored under `~/.local/share/illus/`. There is no central
  database to corrupt.
- **Safe staging**: Installs are staged with `DESTDIR` and moved into
  `~/.local` only after a successful build. Manifests record exactly the files
  the package installed, using paths relative to `~/.local`.
- **Atomic updates**: Manifests are written to a temporary file and renamed
  into place. Failed installs and failed manifest writes are reported instead
  of printing a false success.
- **Hardened pulls**: `pull` clones into a temporary sibling and swaps it into
  place only after the clone succeeds. Existing destinations need `--force`,
  and system paths and symlink escapes are refused.
- **Dry runs and diagnostics**: `--dry-run` shows what a mutating command would
  do, and `illus doctor` checks paths, permissions, tools, manifests, and stale
  state.
- **Build system detection**: Auto-detects `Makefile` and `CMakeLists.txt` with
  prefix injection.
- **Package or binary lookup**: `remove` accepts either the package name or an
  installed binary name.
- **Root refusal**: Mutating commands refuse to run as root unless
  `ILLUS_ALLOW_ROOT=1` is set.

## Quick start

Build and install `illus`:

```bash
git clone https://github.com/campbell-nick/Dreams.git
cd Dreams
make
make install
```

Install a package by name, or directly from a URL:

```bash
illus install <name>
illus install https://github.com/<owner>/<repo>.git
```

List, inspect, and remove packages:

```bash
illus list
illus info <name>
illus remove <name>
```

Make sure `~/.local/bin` is on your `PATH`. The original flags still work:
`illus --list`, `illus --info <name>`, `illus --shed <name>`, `illus --cast
--store`, and so on.

## Documentation

| Document | Contents |
| --- | --- |
| [Warnings and safety](docs/Warnings.md) | Risk warning, safe-usage rules, known limitations |
| [Installation](docs/Installation.md) | Requirements, building/installing `illus`, removing it |
| [Usage](docs/Usage.md) | Commands, registry format, manifests, paths, exit status |
| [Troubleshooting](docs/Troubleshooting.md) | Common errors, `illus doctor`, fixes |
