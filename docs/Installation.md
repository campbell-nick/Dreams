# Installation

[Back to README](../README.md)

## Requirements

- Linux or macOS (other POSIX systems may work; Windows is not currently supported)
- A C99 compiler (GCC or Clang)
- `make` and/or `cmake`, depending on the project being built
- `git` for `install`, `pull`, `update`, and the git-backed registry
- Standard coreutils (`install`, `mkdir`, `rm`, ...)

`illus` uses POSIX APIs (`fork`, `execvp`, `dirent`, `pwd`, `lchown`,
`mkdtemp`), so Windows is not supported.

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

## Registry access

`illus install <name>`, `illus search`, and `illus info <name>` use a registry.
The default is the `registry/packages.tsv` file in this repository, fetched with
`git clone` into `~/.cache/illus/registry` on first use. No registry is needed
if you always install from a URL or local path. To use your own registry:

```bash
export ILLUS_REGISTRY="$HOME/my-registry/packages.tsv"
# or a directory containing packages.tsv or registry/packages.tsv
export ILLUS_REGISTRY="$HOME/my-registry"
# or a git repository
export ILLUS_REGISTRY="https://github.com/example/registry.git"
```

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

The integration tests run against a temporary `ILLUS_HOME` and a local
registry, so they never touch your real `~/.local` and do not need network
access. CI builds and tests on Linux (GCC and Clang) and macOS (Clang), plus a
Linux ASan/UBSan job.
