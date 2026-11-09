# Changelog

This project aims to follow [Semantic Versioning](https://semver.org/) for tagged releases.

## Unreleased / 1.3.0(dev)

### Added
- **Interrupt cleanup**: `SIGINT`/`SIGTERM`/`SIGHUP` now stop the running clone or download and remove its `.illus-clone-*` temporary directory instead of leaving it behind. `doctor` also reports stale clone directories in `~/Dreams` and the current directory.
- **Clone tool indicator**: `pull` and `install` print `Clone tool: cgc` (or `Clone tool: git (<reason>)`) before cloning, so the active clone implementation is visible without `--verbose`.
- **`upgrade <package>`**: reinstalls an installed package using the source URL (and branch, when recorded) from its manifest. Supports `--dry-run`, `--reinstall`, build options, and `--sha256` for tarball sources.
- **Meson/Ninja build support**: auto-detects `meson.build` (`--build-system=meson`), runs `meson setup/compile/install` with `--prefix` and `DESTDIR` staging.
- Improved failure diagnostics: failed commands report exit status and command context; `--verbose` echoes commands.
- `doctor` checks for download and SHA-256 tools, plus `meson`/`ninja`.

### Changed
- **URL-first installs**: `illus install` now takes a git or tarball URL/path. Tarball URLs require `--sha256`.
- **`pull` default destination**: `illus pull <url>` now clones into `./<repo-name>` in the current directory (like `git clone`), instead of `~/Dreams/<repo>`. Use `--dir ~/Dreams/<name>` to restore the old behavior.
- **Clone tool preference**: `illus` now prefers `cgc` (when available on `PATH`) for HTTPS clones, falling back to `git clone` otherwise. `git` is still required for updates (`git pull --ff-only`) and non-HTTP transports.
- `info` reports installed manifest details only.

### Removed
- Name-based registry lookup (`illus install <name>`).
- `illus search`.
- Registry/recipe tap support, `--registry`, `--refresh`, and `ILLUS_REGISTRY`.
- Bundled `registry/` data.

### Kept
- Make/CMake/Autotools/Meson detection (including Autotools bootstrap with `autoreconf` when needed).
- `DESTDIR` staging, stateless manifests under `~/.local/share/illus/`, `list`, `remove`/`shed`, `update`, `pull`, `cast`, `doctor`, `--dry-run`, and safety checks.

### Compatibility
- Existing installed packages and `~/.local/share/illus/*.txt` manifests remain manageable with `list`, `info`, and `remove`. Manifest-format compatibility is a priority; avoid upgrades that prevent removal/inspection of previously installed packages.
- Rolling back `illus` (installing an older tagged version from source) should not modify existing manifests or installed package files.

## 1.2.0

- Added per-package recipe/TSV registry support, Autotools builds, and checksum-verified tarball handling (superseded by the 1.3.0 URL-first direction).

## 1.1.0

- Added subcommand UX, registry-backed installs, safer manifest/staging/pull behavior, diagnostics, and CI.
