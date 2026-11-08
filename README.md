# Illution (illus)

✦ Dreams Package Manager (illus) v1.3.0(dev) ✦

[![CI](https://github.com/campbell-nick/Dreams/actions/workflows/ci.yml/badge.svg)](https://github.com/campbell-nick/Dreams/actions/workflows/ci.yml)

A lightweight, stateless C package manager that builds and installs software
into user space (`~/.local/`) from a git or tarball URL. It detects Make,
CMake, and Autotools projects, stages installs with `DESTDIR`, and tracks every
installed file in a manifest for clean removal and verification.

> **Warning:** This project is still in development. You may face bugs or
> issues. Please report them if you find any. Read
> [Warnings and safety](docs/Warnings.md) before installing anything.

## Features

- **URL installs**: `illus install <git-or-tarball-url>` fetches the source,
  builds it, and installs into `~/.local`. Tarball URLs require `--sha256`.
- **Build system detection**: Auto-detects Make, CMake, Autotools, and Meson.
  CMake/Meson take precedence when multiple systems are present as appropriate;
  Autotools can bootstrap with `autoreconf` when `configure` is missing.
- **Stateless manifests**: The package name comes from the directory/URL you
  install (or `--name`), and manifests are stored under
  `~/.local/share/illus/`. There is no central database to corrupt.
- **Safe staging**: Installs are staged with `DESTDIR` and moved into
  `~/.local` only after a successful build. Manifests record exactly the files
  the package installed, using paths relative to `~/.local`.
- **Atomic updates**: Manifests are written to a temporary file and renamed
  into place. Failed installs and failed manifest writes are reported instead
  of printing a false success.
- **Hardened fetches**: Git clones and tarball downloads/extractions are staged
  in a temporary sibling and swapped into place only after success. Tarballs
  require a SHA-256 checksum. Existing destinations need `--force`, and system
  paths and symlink escapes are refused. For HTTPS clones, `illus` prefers
  [`cgc`](https://github.com/campbell-nick/CGC) when it is available on
  `PATH` (falling back to `git clone` otherwise).
- **Dry runs and diagnostics**: `--dry-run` shows what a mutating command would
  do, and `illus doctor` checks paths, permissions, tools, manifests, and stale
  state.
- **Package or binary lookup**: `remove`/`info` accept either the package name
  or an installed binary name where applicable.
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

Install a package from a URL:

```bash
illus install https://github.com/<owner>/<repo>.git
illus install https://github.com/aspiers/stow.git --branch v2.4.1 --name stow
illus install https://example.org/pkg-1.0.tar.gz --name pkg --sha256 <64-char-hex>
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
| [Usage](docs/Usage.md) | Commands, manifests, paths, exit status |
| [Compatibility and releases](docs/Compatibility.md) | Distro notes, manifest compatibility, rollback/releases |
| [Troubleshooting](docs/Troubleshooting.md) | Common errors, `illus doctor`, fixes |
| [Changelog](CHANGELOG.md) | Release notes and behavior changes |
