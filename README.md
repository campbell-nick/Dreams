# Illution (illus)

✦ Dreams Package Manager (illus) v1.0.0 ✦

A lightweight, stateless C package manager that builds and installs software
into user space (`~/.local/`). It is sudo-aware when resolving the real user's
home directory, but package operations should not be run as root.

> **Warning:** This project is still in development. You may face bugs or
> issues. Please report them if you find any. Read
> [Warnings and safety](docs/Warnings.md) before installing anything.

## Features

- **Stateless design**: Builds from the current working directory and resolves target paths dynamically without a central database.
- **Sudo-aware directory resolution**: Uses `SUDO_USER` and `<pwd.h>` to find the real user's home, so an accidental root invocation still targets the correct user space. Package operations should still be run as your normal user.
- **Ownership fixups**: If a package operation does run as root, installed files are recursively chowned back to the invoking user. This does not make root builds safe.
- **Build system detection**: Auto-detects `Makefile` and `CMakeLists.txt` with prefix injection.
- **Manifest tracking**: Installs are staged with `DESTDIR` and moved into `~/.local` only after a successful build, so manifests record exactly the files the package installed. If a build system ignores `DESTDIR`, illus warns and skips the manifest instead of guessing.
- **Package or binary lookup**: `--shed` accepts either the package/repository name or an installed binary name.

## Quick start

```bash
git clone https://github.com/campbell-nick/Dreams.git
cd Dreams
make
make install

# clone, build, and install a package
illus --pull <url>
cd ~/Dreams/<repo-name>
illus --cast --store
```

List and remove installed packages:

```bash
illus --list
illus --shed <package-or-binary>
```

Make sure `~/.local/bin` is on your `PATH`.

## Documentation

| Document | Contents |
| --- | --- |
| [Warnings and safety](docs/Warnings.md) | Risk warning, safe-usage rules, known limitations |
| [Installation](docs/Installation.md) | Requirements, building/installing `illus`, removing it |
| [Usage](docs/Usage.md) | Paths, package tracking, all commands, exit status |
| [Troubleshooting](docs/Troubleshooting.md) | Common errors and fixes |
