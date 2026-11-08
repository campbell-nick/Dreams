# Changelog

This project aims to follow [Semantic Versioning](https://semver.org/) for tagged releases.

## Unreleased / 1.3.0(dev)

### Changed
- **URL-first installs**: `illus install` now takes a git or tarball URL/path. Tarball URLs require `--sha256`.
- `info` reports installed manifest details only.

### Removed
- Name-based registry lookup (`illus install <name>`).
- `illus search`.
- Registry/recipe tap support, `--registry`, `--refresh`, and `ILLUS_REGISTRY`.
- Bundled `registry/` data.

### Kept
- Make/CMake/Autotools detection (including Autotools bootstrap with `autoreconf` when needed).
- `DESTDIR` staging, stateless manifests under `~/.local/share/illus/`, `list`, `remove`/`shed`, `update`, `pull`, `cast`, `doctor`, `--dry-run`, and safety checks.

### Compatibility
- Existing installed packages and `~/.local/share/illus/*.txt` manifests remain manageable with `list`, `info`, and `remove`. Manifest-format compatibility is a priority; avoid upgrades that prevent removal/inspection of previously installed packages.
- Rolling back `illus` (installing an older tagged version from source) should not modify existing manifests or installed package files.

## 1.2.0

- Added per-package recipe/TSV registry support, Autotools builds, and checksum-verified tarball handling (superseded by the 1.3.0 URL-first direction).

## 1.1.0

- Added subcommand UX, registry-backed installs, safer manifest/staging/pull behavior, diagnostics, and CI.
