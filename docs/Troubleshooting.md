# Troubleshooting

[Back to README](../README.md)

Start with the built-in diagnostics:

```bash
illus doctor
illus doctor --json
illus doctor --verbose
```

`doctor` checks the home directory, root status, `~/.local` ownership and
permissions, `PATH`, required tools, manifest integrity, unsafe entries, stale
staging directories, and untracked binaries.

## Common errors

- **`illus: command not found`** — add `~/.local/bin` to your `PATH`, for
  example `export PATH="$HOME/.local/bin:$PATH"`.
- **`refusing to run <command> as root`** — run `illus` as your normal user.
  `ILLUS_ALLOW_ROOT=1` exists for trusted containers only; it does not make
  root builds safe.
- **`Package '<name>' is not installed`** — the manifest may be missing.
  `remove <binary>` falls back to removing `~/.local/bin/<binary>`, or re-run
  `cast --store` to recreate tracking.
- **`build system ignored DESTDIR; refusing an untracked install`** — the
  project's install rule must respect `$(DESTDIR)$(PREFIX)` (Make) or
  `DESTDIR` with `CMAKE_INSTALL_PREFIX` (CMake). Fix the install rule, or pass
  `--allow-untracked` if you accept an install that `illus` cannot track.
- **`some staged files could not be installed`** — a file could not be moved
  into `~/.local` (permissions, a directory/file conflict, or a full disk).
  The command exits non-zero. Fix the cause and retry; the manifest keeps
  entries for files that were moved successfully.
- **`could not write package manifest`** — the manifest directory is not
  writable, or the disk is full. The old manifest is left in place.
- **`partially removed`** — some files could not be removed (permissions, or
  paths outside `~/.local`). Fix the cause and re-run `remove`; the manifest
  is kept for retries. Do not delete it manually unless you intend to lose
  track of the remaining files.
- **`manifest contains N unsafe entries; refusing to continue`** — a manifest
  line resolves outside `~/.local`, or contains `..`. `illus` refuses to remove
  anything from that manifest. Inspect the file under
  `~/.local/share/illus/<name>.txt`, remove the bad line if you trust the rest,
  and retry. `illus list --verify` and `illus doctor` report the same problem.
- **`destination already exists`** — `install` and `pull` refuse to overwrite
  an existing directory. Use a different `--dir`, or `--force` for destinations
  inside your home directory or `/tmp`/`/var/tmp`. `install` without `--force`
  updates an existing git clone instead.
- **`destination exists but is not a git clone`** — `install` found a directory
  that it cannot update. Move it aside or pass `--force`.
- **`could not update <path>`** — `git pull --ff-only` failed. The clone has
  local changes or has diverged from the remote. Commit/stash/reset the clone,
  or use `install --force` to replace it.
- **`name-based install was removed; '<name>' is not a URL`** — `illus install`
  is URL-first. Pass a git or tarball URL, for example
  `illus install https://github.com/owner/repo.git`.
- **`tarball installs require --sha256 <64-char hex>`** — direct tarball
  installs must be checksum-verified. Obtain the expected SHA-256 from the
  project’s release and pass `--sha256 <hex>`.
- **`SHA-256 mismatch`** — the downloaded archive does not match the expected
  checksum. Do not install it; verify the URL and checksum from a trusted
  source.
- **`configure is missing and autoreconf is not installed`** — install
  Autotools (`autoconf`, `automake`, `libtool`/`autoreconf`) or use a release
  tarball that ships `configure`.
- **`invalid repository URL`** — URLs and paths must not start with `-` or
  contain whitespace or control characters. Use `--` before a URL that begins
  with `-` if you really need to, or use `--name`.
- **`could not derive a package name from the URL`** — pass `--name <pkg>`
  to choose the destination/manifest name explicitly.
- **`cannot determine a valid package name`** — the current directory name is
  not a valid single path component (for example it starts with `.` or `-`).
  Pass `--name <pkg>` or `--allow-untracked`.

## Leftover state

- `~/.local/.illus-stage-*` — temporary staging directories from a crash; safe
  to delete when no `illus` command is running.
- `~/Dreams/.illus-clone-*` — temporary clone/download directories from an
  interrupted fetch; safe to delete when no `illus` command is running.
- `~/Dreams/.illus-old-*` — backup directories from an interrupted `--force`
  replacement. Check that the destination is correct before deleting one.

## See also

- [Warnings and safety](Warnings.md)
- [Usage](Usage.md)
