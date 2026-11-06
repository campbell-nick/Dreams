# Illution (illus)

A lightweight, stateless C package manager designed to build and install software directly into user space (`~/.local/`) while supporting elevated execution via `sudo`.

## WARNING (Use at your own risk)
This is project is still in development. You may face bugs or issues. Feel free to contact me if you face any issues.

## Features

- **Stateless Design**: Uses current working directories (`getcwd()`) and target paths dynamically without needing a centralized local database.
- **Sudo-Aware Directory Resolution**: Automatically resolves the real calling user's home directory (`~/.local/`) using `SUDO_USER` and `<pwd.h>`, ensuring packages are installed into the correct user space even when run under `sudo`.
- **Build System Detection**: Auto-detects `Makefile` and `CMakeLists.txt` build systems with prefix injection.
- **Manifest Tracking**: Automatically tracks installed files for straightforward management.

## Building & Installing `illus`

First clone the repo:
```bash
git clone https://github.com/campbell-nick/Illution.git
```
To build and install `illus` locally to `~/.local/bin`:
```bash
make
make install
```

To build and install `illus` system-wide to `/usr/local/bin`:

```bash
make
sudo make install
```

## Removing

```bash
sudo make uninstall