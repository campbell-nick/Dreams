# Compatibility and releases

[Back to README](../README.md)

## Installed-package compatibility

`illus` stores manifests under `~/.local/share/illus/` and installs files under
`~/.local/`. Compatibility goals:

- Existing manifests remain readable by newer `illus` versions.
- `list`, `info`, `remove`/`shed`, and `--verify` continue to work for packages
  installed by previous compatible versions.
- Upgrading `illus` does not modify installed package files or manifests unless
  you explicitly run an install/remove command.
- Rolling back to an older tagged `illus` build should not break management of
  already-installed packages.

If you hit a manifest compatibility issue, keep a copy of the affected manifest
and the `illus` version that created it, then report the issue.

## Distro and tool compatibility

`illus` targets POSIX/Linux systems and is tested in CI on Ubuntu and macOS.
Gentoo, Arch, Debian/Ubuntu, and Fedora should work when required tools are
installed; Alpine/musl and non-FHS systems are best-effort.

Common requirements:

| Need | Tools |
| --- | --- |
| Git sources | `git` |
| Build | `make`, and/or `cmake`, and/or Autotools (`autoreconf`, `autoconf`, `automake`, `libtool`) depending on the package |
| Tarballs | `tar` (and compression support as needed), `curl` or `wget`, `sha256sum`/`shasum`/`openssl` |
| Archives | `unzip` for `.zip` |

Run `illus doctor` to check paths, permissions, and common tool availability.

Package builds may also require compilers, headers, Perl/Python, pkg-config, or
library dependencies. `illus` does not auto-install system dependencies yet.

## Releases and rollbacks

- Prefer tagged releases for rollback points. Check out a tag and build/install
  `illus` from source:
  ```bash
  git fetch --tags
  git checkout vX.Y.Z
  make
  make install
  ```
- Source archives and checksums attached to GitHub Releases can be used as
  immutable rollback artifacts.
- Before creating a release tag, run `make check` and CMake/CTest; CI should
  pass on the tagged commit.
- Pre-release/dev builds may change behavior; consult `CHANGELOG.md`.

## Versioning

The in-tree display version may include a development suffix (for example
`1.3.0(dev)`). CMake/package metadata may use the numeric version. Release tags
should use `vX.Y.Z` (or `vX.Y.Z-dev.N` for pre-releases) and be documented in
`CHANGELOG.md`.
