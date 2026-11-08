# Warnings and safety

[Back to README](../README.md)

## WARNING (Use at your own risk)

This project is still in development. You may face bugs or issues. Please
report them if you find any.

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
