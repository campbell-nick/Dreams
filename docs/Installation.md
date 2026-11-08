# Installation

[Back to README](../README.md)

## Requirements

- Linux or macOS (other POSIX systems may work; Windows is not currently supported)
- A C99 compiler (GCC or Clang)
- `git` for git URL installs, `pull`, and `update`
- Optional but recommended: [`cgc`](https://github.com/campbell-nick/CGC) for
  faster HTTPS clones. When `cgc` is on `PATH`, `illus` prefers it for HTTPS
  URLs and falls back to `git clone` otherwise. `git` is still required for
  updating existing clones and for local/SSH transports.
- Build tools as needed by packages: `make`, `cmake`, and/or Autotools
  (`autoconf`, `automake`, `libtool`/`autoreconf`) for projects that need
  bootstrapping
- `tar` for tarball sources (`unzip` for `.zip`), plus `curl` or `wget` and a
  SHA-256 tool (`sha256sum`, `shasum`, or `openssl`) for verified downloads
- Standard coreutils (`install`, `mkdir`, `rm`, ...)

`illus` uses POSIX APIs (`fork`, `execvp`, `dirent`, `pwd`, `lchown`,
`mkdtemp`), so Windows is not supported. Use `illus doctor` to check for common
missing tools.

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

You can also build with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build
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

This removes only the `illus` binary. Packages installed with `cast --store` or
`install` must be removed with `illus remove <package-or-binary>`. See
[Usage](Usage.md).

## Running the tests

```bash
make check
```

or with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The integration tests run against a temporary `ILLUS_HOME`, so they never touch your real `~/.local`.
Some cases may need network access for remote URL installs; core cases use local
sources where possible. CI builds and tests on Linux (GCC and Clang) and macOS (Clang), plus a
Linux ASan/UBSan job.
