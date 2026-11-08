#!/bin/sh
# Integration tests for illus.
#
# Usage: tests/integration.sh /path/to/illus
#
# The tests use ILLUS_HOME to redirect all package state into a temporary
# directory, so the real ~/.local is never touched. A local registry and local
# git repositories are used so the suite never needs network access.

set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/illus" >&2
    exit 2
fi

BIN=$1
case "$BIN" in
    */*) ;;
    *) BIN=$(command -v "$BIN" 2>/dev/null || printf '%s' "$BIN") ;;
esac
case "$BIN" in
    /*) ;;
    *) BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN") ;;
esac
if [ ! -x "$BIN" ]; then
    echo "error: not executable: $BIN" >&2
    exit 2
fi

# Use /tmp explicitly: on macOS $TMPDIR lives under /var/folders, which is
# treated as a system area by illus.
TMP=$(mktemp -d /tmp/illus-test.XXXXXX)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

export ILLUS_HOME="$TMP/home"
mkdir -p "$ILLUS_HOME"
BINDIR="$ILLUS_HOME/.local/bin"
MDIR="$ILLUS_HOME/.local/share/illus"
DREAMS="$ILLUS_HOME/Dreams"

# GitHub Actions and most containers do not run as root, but allow the suite
# to work inside a root container. The root-refusal path is tested separately.
export ILLUS_ALLOW_ROOT=1

fail() {
    echo "FAIL: $*" >&2
    if [ -f "$TMP/last.log" ]; then
        cat "$TMP/last.log" >&2
    fi
    exit 1
}

run() {
    if ! "$@" >"$TMP/last.log" 2>&1; then
        fail "command failed: $*"
    fi
}

run_fail() {
    if "$@" >"$TMP/last.log" 2>&1; then
        fail "command unexpectedly succeeded: $*"
    fi
}

expect_status() {
    want=$1
    shift
    set +e
    "$@" >"$TMP/last.log" 2>&1
    rc=$?
    set -e
    if [ "$rc" -ne "$want" ]; then
        fail "expected exit $want, got $rc: $*"
    fi
}

run_in() {
    dir=$1
    shift
    if ! ( cd "$dir" && "$@" ) >"$TMP/last.log" 2>&1; then
        fail "command failed in $dir: $*"
    fi
}

run_in_fail() {
    dir=$1
    shift
    if ( cd "$dir" && "$@" ) >"$TMP/last.log" 2>&1; then
        fail "command unexpectedly succeeded in $dir: $*"
    fi
}

# Portable "does this directory contain a matching entry?" check. BSD find on
# macOS does not support -maxdepth, so avoid it in the stale-state checks.
has_entry() {
    dir=$1
    pattern=$2

    for path in "$dir"/$pattern; do
        if [ -e "$path" ]; then
            return 0
        fi
    done
    return 1
}

# Create a Makefile project that installs one executable.
make_package() {
    dir=$1
    bin=$2
    mkdir -p "$dir"
    printf '#!/bin/sh\necho test\n' > "$dir/hello"
    chmod +x "$dir/hello"
    cat > "$dir/Makefile" <<EOF
all:
	@true
install:
	install -d \$(DESTDIR)\$(PREFIX)/bin
	install -m 755 hello \$(DESTDIR)\$(PREFIX)/bin/$bin
EOF
}

init_repo() {
    dir=$1
    run git -C "$dir" init -q
    run git -C "$dir" config user.email test@example.com
    run git -C "$dir" config user.name Test
}

commit_all() {
    dir=$1
    msg=$2
    run git -C "$dir" add -A
    run git -C "$dir" commit -qm "$msg"
}

echo "== CLI =="
run "$BIN" --version
grep -q "Dreams Package Manager" "$TMP/last.log" || fail "version output"
run "$BIN" --help
grep -q "install <name|url>" "$TMP/last.log" || fail "help output"
run "$BIN" help
run "$BIN" help install
grep -q -- "--reinstall" "$TMP/last.log" || fail "install help"
expect_status 2 "$BIN" frobnicate
grep -q "unknown command" "$TMP/last.log" || fail "unknown command message"
expect_status 2 "$BIN" list --bogus
grep -q "unknown option" "$TMP/last.log" || fail "unknown option message"
expect_status 2 "$BIN" pull /tmp/x --dir
grep -q "requires a value" "$TMP/last.log" || fail "missing value message"
expect_status 2 "$BIN" install
expect_status 2 "$BIN" info
expect_status 2 "$BIN" remove

echo "== Make package: cast/store/list/info/shed =="
SRC="$TMP/src/make-pkg"
make_package "$SRC" illus-test-make
run_in "$SRC" "$BIN" cast --store
[ -x "$BINDIR/illus-test-make" ] || fail "Make package binary not installed"
[ -f "$MDIR/make-pkg.txt" ] || fail "Make package manifest missing"
head -n 1 "$MDIR/make-pkg.txt" | grep -q "^# illus-manifest-v2$" ||
    fail "manifest is not v2"
grep -q "^# package: make-pkg$" "$MDIR/make-pkg.txt" ||
    fail "manifest package metadata missing"
grep -q "^bin/illus-test-make$" "$MDIR/make-pkg.txt" ||
    fail "manifest path is not relative"
if grep -q "$ILLUS_HOME/.local/bin" "$MDIR/make-pkg.txt"; then
    fail "manifest still stores absolute paths"
fi
run "$BIN" list
grep -q "make-pkg" "$TMP/last.log" || fail "list did not show make-pkg"
run "$BIN" list --json
grep -q '"name":"make-pkg"' "$TMP/last.log" || fail "list --json"
run "$BIN" list --verbose
grep -q "source:" "$TMP/last.log" || fail "list --verbose"
run "$BIN" list --verify
run "$BIN" info make-pkg --json
grep -q '"installed":true' "$TMP/last.log" || fail "info --json"
run "$BIN" info illus-test-make --files
grep -q "$BINDIR/illus-test-make" "$TMP/last.log" ||
    fail "info did not resolve a binary name"
run "$BIN" remove make-pkg --dry-run
[ -x "$BINDIR/illus-test-make" ] || fail "dry-run removed the binary"
[ -f "$MDIR/make-pkg.txt" ] || fail "dry-run removed the manifest"
run "$BIN" shed make-pkg
[ ! -e "$BINDIR/illus-test-make" ] || fail "Make package binary not removed"
[ ! -e "$MDIR/make-pkg.txt" ] || fail "Make package manifest not removed"

echo "== shed with an already-missing file =="
SRC="$TMP/src/missing-pkg"
make_package "$SRC" illus-test-missing
run_in "$SRC" "$BIN" --cast --store
rm -f "$BINDIR/illus-test-missing"
run "$BIN" --shed missing-pkg
[ ! -e "$MDIR/missing-pkg.txt" ] || fail "manifest not removed after missing file"

echo "== CMake package: cast/store/shed =="
SRC="$TMP/src/cmake-pkg"
mkdir -p "$SRC"
cat > "$SRC/main.c" <<'EOF'
int main(void) { return 0; }
EOF
cat > "$SRC/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.10)
project(cmake_pkg C)
add_executable(illus-test-cmake main.c)
install(TARGETS illus-test-cmake RUNTIME DESTINATION bin)
EOF
run_in "$SRC" "$BIN" cast --store
[ -x "$BINDIR/illus-test-cmake" ] || fail "CMake package binary not installed"
[ -f "$MDIR/cmake-pkg.txt" ] || fail "CMake package manifest missing"
grep -q "^bin/illus-test-cmake$" "$MDIR/cmake-pkg.txt" ||
    fail "CMake manifest path is not relative"
run "$BIN" remove cmake-pkg
[ ! -e "$BINDIR/illus-test-cmake" ] || fail "CMake package binary not removed"
[ ! -e "$MDIR/cmake-pkg.txt" ] || fail "CMake package manifest not removed"

echo "== untracked installs and install failures =="
SRC="$TMP/src/untracked"
make_package "$SRC" illus-test-untracked
cat > "$SRC/Makefile" <<'EOF'
all:
	@true
install:
	install -d $(PREFIX)/bin
	install -m 755 hello $(PREFIX)/bin/illus-test-untracked
EOF
run_in_fail "$SRC" env -u DESTDIR "$BIN" --cast --store
grep -q "ignored DESTDIR" "$TMP/last.log" ||
    fail "untracked install did not warn"
run_in "$SRC" "$BIN" cast --store --allow-untracked
[ -x "$BINDIR/illus-test-untracked" ] || fail "untracked binary missing"
[ ! -e "$MDIR/untracked.txt" ] || fail "untracked install wrote a manifest"
run "$BIN" remove illus-test-untracked
[ ! -e "$BINDIR/illus-test-untracked" ] || fail "untracked binary not removed"

SRC="$TMP/src/broken"
mkdir -p "$SRC"
cat > "$SRC/Makefile" <<'EOF'
all:
	@true
install:
	@echo "install failed on purpose" >&2
	@exit 1
EOF
run_in_fail "$SRC" env -u DESTDIR "$BIN" --cast --store
grep -q "installation failed" "$TMP/last.log" ||
    fail "install failure was not reported"
if grep -q "Successfully built and installed" "$TMP/last.log"; then
    fail "install failure printed a success message"
fi

echo "== manifest safety and list --verify =="
SRC="$TMP/src/unsafe-pkg"
make_package "$SRC" illus-test-unsafe
run_in "$SRC" "$BIN" cast --store
cp "$MDIR/unsafe-pkg.txt" "$TMP/unsafe-pkg.txt.bak"
printf '../../etc/passwd\n' >> "$MDIR/unsafe-pkg.txt"
run_fail "$BIN" list --verify
grep -q "Verification failed" "$TMP/last.log" ||
    fail "list --verify did not report unsafe entries"
run_fail "$BIN" doctor --json
grep -q '"status":"fail"' "$TMP/last.log" ||
    fail "doctor did not report unsafe entries"
run_fail "$BIN" remove unsafe-pkg
grep -q "unsafe" "$TMP/last.log" || fail "remove accepted unsafe entries"
cp "$TMP/unsafe-pkg.txt.bak" "$MDIR/unsafe-pkg.txt"
run "$BIN" remove unsafe-pkg
[ ! -e "$MDIR/unsafe-pkg.txt" ] || fail "unsafe package manifest not removed"

echo "== pull =="
REPO="$TMP/repo"
mkdir -p "$REPO"
printf 'hello\n' > "$REPO/file.txt"
init_repo "$REPO"
commit_all "$REPO" init
run "$BIN" pull "$REPO/" --dir "$TMP/clone" --dry-run
[ ! -e "$TMP/clone" ] || fail "pull --dry-run created a directory"
run "$BIN" pull "$REPO/" --dir "$TMP/clone"
[ -f "$TMP/clone/file.txt" ] || fail "pull did not clone"
run_fail "$BIN" pull "$REPO" --dir "$TMP/clone"
grep -q "destination already exists" "$TMP/last.log" ||
    fail "pull overwrote an existing destination"
run "$BIN" pull "$REPO" --dir "$TMP/clone" --force --dry-run
[ -f "$TMP/clone/file.txt" ] || fail "pull --force --dry-run changed the clone"
run "$BIN" pull --dir "$TMP/clone2" -- "$REPO"
[ -f "$TMP/clone2/file.txt" ] || fail "pull with -- did not clone"
run "$BIN" pull "$REPO" --name custom-name
[ -d "$DREAMS/custom-name" ] || fail "pull --name destination missing"
run "$BIN" pull "$REPO" --dir "$TMP/shallow" --depth 1
[ -f "$TMP/shallow/file.txt" ] || fail "pull --depth did not clone"
expect_status 1 "$BIN" pull -- -not-a-url
grep -q "invalid repository URL" "$TMP/last.log" ||
    fail "pull accepted an option-like URL"

ln -s /etc "$TMP/evil"
run_fail "$BIN" pull "$REPO" --dir "$TMP/evil/illus-should-not-exist"
[ ! -e /etc/illus-should-not-exist ] || fail "pull escaped through a symlink"

echo "== registry: search/info/install/upgrade =="
REG="$TMP/registry"
mkdir -p "$REG"
HELLO="$TMP/src/hello-git"
make_package "$HELLO" hello
init_repo "$HELLO"
commit_all "$HELLO" init
printf 'hello\t%s\t\tSay hello\n' "$HELLO" > "$REG/packages.tsv"
export ILLUS_REGISTRY="$REG"

run "$BIN" search hello
grep -q "Say hello" "$TMP/last.log" || fail "search did not find hello"
run "$BIN" search --json
grep -q '"name":"hello"' "$TMP/last.log" || fail "search --json"
run "$BIN" info hello
grep -q "not installed" "$TMP/last.log" ||
    fail "info did not use the registry"
run "$BIN" install hello --dry-run
[ ! -d "$DREAMS/hello" ] || fail "install --dry-run cloned"
run "$BIN" install hello
[ -x "$BINDIR/hello" ] || fail "registry install did not install hello"
grep -q "^# source: $HELLO$" "$MDIR/hello.txt" ||
    fail "install did not record the source"
run "$BIN" list
grep -q "hello" "$TMP/last.log" || fail "list did not show hello"

printf '#!/bin/sh\necho version-two\n' > "$HELLO/hello"
commit_all "$HELLO" "version two"
run "$BIN" install hello
grep -q "version-two" "$BINDIR/hello" ||
    fail "install did not upgrade an existing package"
run "$BIN" install hello --reinstall
grep -q "Sweeping clean build artifacts" "$TMP/last.log" ||
    fail "install --reinstall did not use a fresh build"

run "$BIN" info hello --files
grep -q "$BINDIR/hello" "$TMP/last.log" || fail "info --files"

run "$BIN" install hello --name hello-copy
[ -x "$BINDIR/hello" ] || fail "install --name broke the install"
[ -f "$MDIR/hello-copy.txt" ] || fail "install --name manifest missing"
run "$BIN" remove hello-copy

run "$BIN" remove hello
[ ! -e "$BINDIR/hello" ] || fail "hello binary not removed"
[ ! -e "$MDIR/hello.txt" ] || fail "hello manifest not removed"
run "$BIN" info hello
grep -q "available in registry" "$TMP/last.log" ||
    fail "info after remove did not fall back to the registry"

run_fail "$BIN" install no-such-package
grep -q "was not found in the registry" "$TMP/last.log" ||
    fail "install accepted an unknown package"

echo "== direct URL install =="
URLPKG="$TMP/src/url-pkg"
make_package "$URLPKG" url-tool
init_repo "$URLPKG"
commit_all "$URLPKG" init
run "$BIN" install "$URLPKG"
[ -x "$BINDIR/url-tool" ] || fail "URL install did not install"
[ -f "$MDIR/url-pkg.txt" ] || fail "URL install manifest missing"
run "$BIN" remove url-pkg

echo "== git-backed registry and refresh =="
REGREPO="$TMP/registry-repo"
mkdir -p "$REGREPO/registry"
printf 'hello\t%s\t\tSay hello\n' "$HELLO" > "$REGREPO/registry/packages.tsv"
init_repo "$REGREPO"
commit_all "$REGREPO" init
unset ILLUS_REGISTRY
export ILLUS_REGISTRY="file://$REGREPO"
run "$BIN" search hello
[ -f "$ILLUS_HOME/.cache/illus/registry/registry/packages.tsv" ] ||
    fail "git registry was not cached"
printf 'hello\t%s\t\tSay hello\nsecond\t%s\t\tSecond package\n' \
    "$HELLO" "$HELLO" > "$REGREPO/registry/packages.tsv"
commit_all "$REGREPO" "add second"
run "$BIN" search second --refresh
grep -q "Second package" "$TMP/last.log" ||
    fail "registry refresh did not fetch the new entry"
unset ILLUS_REGISTRY

echo "== update =="
run "$BIN" install hello
run "$BIN" update --dry-run
grep -q "would update hello" "$TMP/last.log" ||
    fail "update --dry-run did not list hello"
printf '#!/bin/sh\necho version-three\n' > "$HELLO/hello"
commit_all "$HELLO" "version three"
run "$BIN" update
grep -q "version-three" "$BINDIR/hello" ||
    fail "update did not rebuild the package"
run "$BIN" remove hello

echo "== list --all and doctor =="
printf '#!/bin/sh\ntrue\n' > "$BINDIR/untracked-tool"
chmod +x "$BINDIR/untracked-tool"
run "$BIN" list --all
grep -q "untracked-tool (untracked)" "$TMP/last.log" ||
    fail "list --all did not show an untracked binary"
run "$BIN" list --all --json
grep -q '"name":"untracked-tool","tracked":false' "$TMP/last.log" ||
    fail "list --all --json did not show an untracked binary"
rm -f "$BINDIR/untracked-tool"

run "$BIN" doctor --json
grep -q '"check":"home directory"' "$TMP/last.log" ||
    fail "doctor --json output"
run "$BIN" doctor --verbose
grep -q "paths: local=" "$TMP/last.log" || fail "doctor --verbose output"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        openssl dgst -sha256 "$1" | awk '{print $NF}'
    fi
}

echo "== recipe registry: lookup/search/info and TSV override =="
RREG="$TMP/recipe-registry"
mkdir -p "$RREG/registry/packages"
RHELLO="$TMP/src/recipe-hello"
make_package "$RHELLO" recipe-hello
init_repo "$RHELLO"
commit_all "$RHELLO" init
cat > "$RREG/registry/packages/recipe-hello.conf" <<EOF
name=recipe-hello
version=1.2.3
url=$RHELLO
branch=
build=make
source=git
description=Recipe hello package
depends=make
homepage=https://example.invalid/recipe-hello
EOF
printf 'recipe-hello\t%s\t\tOld TSV description\toverride\tgit\t\t0.0.1\n' \
    "$RHELLO" > "$RREG/registry/packages.tsv"
export ILLUS_REGISTRY="$RREG"
run "$BIN" search recipe-hello --json
grep -q '"name":"recipe-hello"' "$TMP/last.log" || fail "recipe search"
grep -q '"version":"1.2.3"' "$TMP/last.log" || fail "recipe search version"
if grep -q 'Old TSV description' "$TMP/last.log"; then
    fail "recipe did not override TSV row"
fi
run "$BIN" info recipe-hello --json
grep -q '"installed":false' "$TMP/last.log" || fail "recipe info installed"
grep -q '"build":"make"' "$TMP/last.log" || fail "recipe info build"
grep -q '"depends":"make"' "$TMP/last.log" || fail "recipe info depends"
run "$BIN" install recipe-hello
[ -x "$BINDIR/recipe-hello" ] || fail "recipe install did not install"
run "$BIN" remove recipe-hello
unset ILLUS_REGISTRY

echo "== tarball install: checksum success and mismatch =="
TARSRC="$TMP/tar-src/tar-pkg-1.0"
mkdir -p "$TARSRC"
cat > "$TARSRC/main.c" <<'EOF'
int main(void) { return 0; }
EOF
cat > "$TARSRC/Makefile" <<'EOF'
PREFIX ?= /usr/local
all:
	cc -o tar-hello main.c
install:
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 tar-hello $(DESTDIR)$(PREFIX)/bin/tar-hello
clean:
	rm -f tar-hello
EOF
TARBALL="$TMP/tarballs/tar-pkg-1.0.tar.gz"
mkdir -p "$TMP/tarballs"
tar -czf "$TARBALL" -C "$TMP/tar-src" tar-pkg-1.0
GOODSUM=$(sha256_of "$TARBALL")
run "$BIN" install "$TARBALL" --name tar-pkg --sha256 "$GOODSUM"
[ -x "$BINDIR/tar-hello" ] || fail "tarball install did not install"
[ -f "$MDIR/tar-pkg.txt" ] || fail "tarball manifest missing"
run "$BIN" remove tar-pkg
run_fail "$BIN" install "$TARBALL" --name tar-bad --sha256 0000000000000000000000000000000000000000000000000000000000000000
grep -q "SHA-256 mismatch" "$TMP/last.log" || fail "tarball mismatch not detected"
[ ! -d "$DREAMS/tar-bad" ] || fail "failed tarball left destination behind"
[ ! -e "$BINDIR/tar-hello" ] || fail "failed tarball install leaked files"
run_fail "$BIN" install "$TARBALL" --name tar-no-sum
grep -q "require a 64-character sha256" "$TMP/last.log" ||
    fail "tarball without checksum was accepted"

echo "== Autotools project and autoreconf bootstrap =="
ASRC="$TMP/src/autotools-pkg"
mkdir -p "$ASRC"
cat > "$ASRC/main.c" <<'EOF'
int main(void) { return 0; }
EOF
cat > "$ASRC/configure" <<'EOF'
#!/bin/sh
set -eu
prefix=/usr/local
for arg in "$@"; do
    case "$arg" in
        --prefix=*) prefix=${arg#--prefix=} ;;
    esac
done
cat > Makefile <<MK
PREFIX = $prefix
all:
	cc -o auto-hello main.c
install:
	install -d \$(DESTDIR)\$(PREFIX)/bin
	install -m 755 auto-hello \$(DESTDIR)\$(PREFIX)/bin/auto-hello
distclean: clean
	rm -f Makefile config.status
clean:
	rm -f auto-hello
MK
EOF
chmod +x "$ASRC/configure"
cat > "$ASRC/configure.ac" <<'EOF'
AC_INIT([autotools-pkg], [1.0])
EOF
cat > "$ASRC/Makefile.am" <<'EOF'
# fake
EOF
run_in "$ASRC" "$BIN" cast --store
[ -x "$BINDIR/auto-hello" ] || fail "Autotools binary not installed"
grep -q "^bin/auto-hello$" "$MDIR/autotools-pkg.txt" ||
    fail "Autotools manifest path wrong"
run "$BIN" remove autotools-pkg

echo "== Autotools bootstrap with fake autoreconf =="
BSRC="$TMP/src/bootstrap-pkg"
mkdir -p "$BSRC"
cat > "$BSRC/main.c" <<'EOF'
int main(void) { return 0; }
EOF
cat > "$BSRC/configure.ac" <<'EOF'
AC_INIT([bootstrap-pkg], [1.0])
EOF
cat > "$BSRC/Makefile.am" <<'EOF'
# fake
EOF
FAKEBIN="$TMP/fakebin"
mkdir -p "$FAKEBIN"
cat > "$FAKEBIN/autoreconf" <<'EOF'
#!/bin/sh
set -eu
cat > configure <<'CFG'
#!/bin/sh
set -eu
prefix=/usr/local
for arg in "$@"; do
    case "$arg" in
        --prefix=*) prefix=${arg#--prefix=} ;;
    esac
done
cat > Makefile <<MK
PREFIX = $prefix
all:
	cc -o boot-hello main.c
install:
	install -d \$(DESTDIR)\$(PREFIX)/bin
	install -m 755 boot-hello \$(DESTDIR)\$(PREFIX)/bin/boot-hello
distclean: clean
	rm -f Makefile
clean:
	rm -f boot-hello
MK
CFG
chmod +x configure
EOF
chmod +x "$FAKEBIN/autoreconf"
OLDPATH=$PATH
PATH="$FAKEBIN:$PATH"
run_in "$BSRC" "$BIN" cast --store
PATH=$OLDPATH
[ -x "$BINDIR/boot-hello" ] || fail "bootstrapped Autotools binary not installed"
run "$BIN" remove bootstrap-pkg

echo "== stale state and manifest cleanup =="
if has_entry "$MDIR" '.manifest-*'; then
    fail "temporary manifest files were left behind"
fi
if has_entry "$ILLUS_HOME/.local" '.illus-stage-*'; then
    fail "staging directories were left behind"
fi
if has_entry "$DREAMS" '.illus-clone-*'; then
    fail "clone directories were left behind"
fi

if [ "$(id -u)" -eq 0 ]; then
    echo "== root refusal =="
    run_fail env ILLUS_ALLOW_ROOT=0 "$BIN" pull "$REPO" --dir "$TMP/root-check"
    grep -q "refusing to run pull as root" "$TMP/last.log" ||
        fail "root refusal message missing"
fi

echo "All integration tests passed."
