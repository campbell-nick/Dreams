# Warnings and safety

[Back to README](../README.md)

## WARNING (Use at your own risk)

This project is still in development. You may face bugs or issues. Please
report them if you find any.

## Safe usage

Dreams runs the build system of the project it installs, so treat it like
running `make install` yourself.

- **Do not run mutating commands as root** (including via `sudo` or `doas`).
  `install`, `cast`, `update`, `pull`, and `remove` refuse to run when
  `geteuid() == 0` unless `ILLUS_ALLOW_ROOT=1` is set. Running `sudo make
  install` for `illus` itself is fine, but the package manager must run as your
  normal user. The escape hatch exists only for trusted containers; it does not
  make root builds safe.
- Ensure `~/.local`, `~/Dreams`, and the registry cache are owned by you and
  are not world-writable.
- Run one `illus` command at a time. There is no locking.
- If `remove` reports that a package was only partially removed, keep its
  manifest in `~/.local/share/illus/` and retry.
- Avoid `--force` on destinations you care about. It replaces the existing
  clone after the new clone succeeds, but the old directory is still deleted.
- Only install repositories and registries you trust; packages are not
  sandboxed, signed, or verified. A registry entry is just a URL: whoever
  controls the registry can point a name at any repository, and the build
  scripts in that repository run with your privileges.
- `--allow-untracked` accepts an install that ignores `DESTDIR`. Those files
  cannot be tracked or cleanly removed by `illus`.

## Known limitations

- There is no locking, so concurrent `illus` processes are not supported.
- Filesystem containment checks are best-effort and do not guard against a
  malicious local process racing symlinks inside `~/.local`.
- `~/.local` and the home directory are assumed to be real directories, not
  symlinks.
- Manifest writes are atomic, but a package install is not transactional: a
  failure while moving staged files can leave some files installed. The
  manifest keeps entries for files that were moved successfully so a retry or
  `remove` can clean up.
- The registry has no signature or trust model. Refreshes use `git pull
  --ff-only` from the configured URL.
- A package name and an installed binary name can collide; `remove` refuses to
  guess when multiple manifests provide the same binary.
