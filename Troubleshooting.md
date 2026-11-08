# Troubleshooting

[Back to README](README.md)

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

## See also

- [Warnings and safety](Warnings.md)
- [Usage](Usage.md)
