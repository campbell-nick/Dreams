# Installation

[Back to README](README.md)

## Requirements

- Linux or another POSIX-like system
- A C99 compiler (GCC or Clang)
- `make` and/or `cmake`, depending on the project being built
- `git` for `--pull` and `--update`
- Standard coreutils (`install`, `mkdir`, `rm`, ...)

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

If you installed locally, make sure `~/.local/bin` is on your `PATH`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

## Removing `illus`

If you installed it locally:

```bash
make uninstall
```

If you installed it system-wide:

```bash
sudo make uninstall
```

This removes only the `illus` binary. Packages installed with
`--cast --store` must be removed with `illus --shed <package-or-binary>`.
See [Usage](Usage.md).
