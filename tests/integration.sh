#!/bin/sh
# Integration tests for illus.
#
# Usage: tests/integration.sh /path/to/illus
#
# The tests use ILLUS_HOME to redirect all package state into a temporary
# directory, so the real ~/.local is never touched.

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

echo "== CLI =="
run "$BIN" --version
run "$BIN" --help

echo "== Make package: cast/store/list/shed =="
SRC="$TMP/src/make-pkg"
make_package "$SRC" illus-test-make
( cd "$SRC" && "$BIN" --cast --store ) >"$TMP/last.log" 2>&1 ||
    fail "cast/store (Make)"
[ -x "$BINDIR/illus-test-make" ] || fail "Make package binary not installed"
[ -f "$MDIR/make-pkg.txt" ] || fail "Make package manifest missing"
run "$BIN" --list
grep -q "make-pkg" "$TMP/last.log" || fail "--list did not show make-pkg"
run "$BIN" --shed make-pkg
[ ! -e "$BINDIR/illus-test-make" ] || fail "Make package binary not removed"
[ ! -e "$MDIR/make-pkg.txt" ] || fail "Make package manifest not removed"

echo "== shed with an already-missing file =="
SRC="$TMP/src/missing-pkg"
make_package "$SRC" illus-test-missing
( cd "$SRC" && "$BIN" --cast --store ) >"$TMP/last.log" 2>&1 ||
    fail "cast/store (missing)"
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
( cd "$SRC" && "$BIN" --cast --store ) >"$TMP/last.log" 2>&1 ||
    fail "cast/store (CMake)"
[ -x "$BINDIR/illus-test-cmake" ] || fail "CMake package binary not installed"
[ -f "$MDIR/cmake-pkg.txt" ] || fail "CMake package manifest missing"
run "$BIN" --shed cmake-pkg
[ ! -e "$BINDIR/illus-test-cmake" ] || fail "CMake package binary not removed"
[ ! -e "$MDIR/cmake-pkg.txt" ] || fail "CMake package manifest not removed"

echo "== pull and symlink escape =="
REPO="$TMP/repo"
mkdir -p "$REPO"
printf 'hello\n' > "$REPO/file.txt"
run git -C "$REPO" init -q
run git -C "$REPO" config user.email test@example.com
run git -C "$REPO" config user.name Test
run git -C "$REPO" add file.txt
run git -C "$REPO" commit -qm init
run "$BIN" --pull "$REPO/" --dir "$TMP/clone"
[ -f "$TMP/clone/file.txt" ] || fail "pull did not clone"

ln -s /etc "$TMP/evil"
run_fail "$BIN" --pull "$REPO" --dir "$TMP/evil/illus-should-not-exist"
[ ! -e /etc/illus-should-not-exist ] || fail "pull escaped through a symlink"

echo "All integration tests passed."
