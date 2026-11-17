#!/bin/sh
# Offline checks for the Linux packager: the refusals that protect an existing release, and the
# desktop entry the package installs. Building an actual package needs a release build and the
# network (appimagetool), so that stays an integration step.
set -eu
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd -P)"
repo="$(CDPATH='' cd -- "$here/../.." && pwd -P)"
packager="$repo/packaging/linux/build-packages.sh"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/sonicator-linux-test.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail() { echo "FAIL: $1" >&2; exit 1; }

# A release build directory with just enough shape for the packager to accept it.
seed() {
    mkdir -p "$1/Sonicator_artefacts/Release"
    cp /bin/true "$1/Sonicator_artefacts/Release/SONICATOR"
    cp /bin/true "$1/Sonicator_artefacts/Release/libonnxruntime.so"
}

# 1. A missing application must be named, not half-packaged.
mkdir -p "$tmp/empty"
if sh "$packager" "$tmp/empty" "$tmp/dist" 2>"$tmp/err"; then
    fail "packaged without an application"
fi
grep -q "Missing application" "$tmp/err" || fail "missing application was not named"

# 2. An existing artifact must never be replaced behind the owner's back.
seed "$tmp/build"
version="$(sed -n 's/^project(Sonicator VERSION \([0-9][0-9.]*\) .*/\1/p' "$repo/CMakeLists.txt")"
mkdir -p "$tmp/dist"
: > "$tmp/dist/SONICATOR-$version-Linux-amd64.deb"
if sh "$packager" "$tmp/build" "$tmp/dist" 2>"$tmp/err"; then
    fail "overwrote an existing package"
fi
grep -q "Refusing to overwrite" "$tmp/err" || fail "overwrite refusal was not explained"

# 3. The desktop entry names the product and the icon the package installs.
desktop="$repo/packaging/linux/sonicator.desktop"
grep -q '^Name=SONICATOR$' "$desktop" || fail "desktop entry has no product name"
grep -q '^Exec=SONICATOR$' "$desktop" || fail "desktop entry does not launch the binary"
grep -q '^Icon=sonicator$' "$desktop" || fail "desktop entry does not name the icon"
for size in 16 32 48 64 128 256 512; do
    test -f "$repo/packaging/linux/icons/${size}x${size}.png" || fail "missing ${size}px icon"
done

# 4. The AppRun entry point starts the binary that the package installs.
grep -q 'usr/bin/SONICATOR' "$repo/packaging/linux/AppRun" || fail "AppRun does not start the app"

echo "linux packaging checks passed"
