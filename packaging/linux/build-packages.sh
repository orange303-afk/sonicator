#!/bin/sh
# Package existing Release artifacts as a Debian package and an AppImage.
# Does not install or publish anything.
#
# The application must have been built on the release baseline (ubuntu:22.04, glibc 2.35): an
# AppImage does not carry glibc, so the build host's glibc becomes every user's floor. The script
# refuses to publish a binary above 2.35 rather than shipping one nobody on an older distro can run.
set -eu
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd -P)"
repo="$(CDPATH='' cd -- "$here/../.." && pwd -P)"
build="${1:?Usage: build-packages.sh BUILD_DIR OUTPUT_DIR}"
output="${2:?Usage: build-packages.sh BUILD_DIR OUTPUT_DIR}"
version="$(sed -n 's/^project(Sonicator VERSION \([0-9][0-9.]*\) .*/\1/p' "$repo/CMakeLists.txt")"
test -n "$version"
glibc_baseline="2.35"

mkdir -p "$output"
output="$(CDPATH='' cd -- "$output" && pwd -P)"
build="$(CDPATH='' cd -- "$build" && pwd -P)"
app="$build/Sonicator_artefacts/Release/SONICATOR"
runtime="$build/Sonicator_artefacts/Release/libonnxruntime.so.1"
test -x "$app" || { echo "Missing application: $app" >&2; exit 1; }
test -f "$runtime" || { echo "Missing runtime: $runtime" >&2; exit 1; }

deb="$output/SONICATOR-$version-Linux-amd64.deb"
image="$output/SONICATOR-$version-Linux-x86_64.AppImage"
for artifact in "$deb" "$image"; do
    test ! -e "$artifact" || { echo "Refusing to overwrite $artifact" >&2; exit 1; }
done

# Staging writes a full copy of the payload; a nearly full disk produces a half package.
available="$(df -Pk "$build" | awk 'NR==2 {print $4}')"
test "$available" -ge 8388608 || { echo 'Less than 8 GiB free; refusing to stage payloads' >&2; exit 1; }

# Pinned so a release build is reproducible and a compromised upstream asset is caught here.
APPIMAGETOOL_VERSION="1.9.1"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
tools="$build/packaging-tools"
appimagetool="$tools/appimagetool-x86_64-$APPIMAGETOOL_VERSION.AppImage"
mkdir -p "$tools"
if [ ! -f "$appimagetool" ]; then
    curl -fsSL -o "$appimagetool.part" \
        "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage"
    echo "$APPIMAGETOOL_SHA256  $appimagetool.part" | sha256sum -c -
    mv "$appimagetool.part" "$appimagetool"
    chmod 755 "$appimagetool"
fi

glibc_ceiling() {
    objdump -T "$1" | sed -n 's/.*GLIBC_\([0-9][0-9.]*\).*/\1/p' | sort -V | tail -1
}

verify_binary() {
    binary="$1"
    name="$(basename "$binary")"
    ceiling="$(glibc_ceiling "$binary")"
    if [ -n "$ceiling" ] && dpkg --compare-versions "$ceiling" gt "$glibc_baseline"; then
        echo "$name needs glibc $ceiling, above the $glibc_baseline baseline" >&2
        exit 1
    fi
    # The loader must look beside the binary, and never into the machine that built it.
    readelf -d "$binary" | grep -q 'ORIGIN' || {
        echo "$name has no \\\$ORIGIN rpath" >&2; exit 1; }
    if readelf -d "$binary" | grep -E 'RPATH|RUNPATH' | grep -qE "$build|/home/|/Users/|/tmp/"; then
        echo "$name has an absolute rpath" >&2
        exit 1
    fi
    echo "verified $name (glibc $ceiling)"
}

stage_tree() {
    root="$1"
    mkdir -p "$root/usr/bin" "$root/usr/lib/sonicator" \
             "$root/usr/share/applications" "$root/usr/share/doc/sonicator"
    cp "$app" "$root/usr/bin/SONICATOR"
    chmod 755 "$root/usr/bin/SONICATOR"
    # The runtime's SONAME is libonnxruntime.so.1; the unversioned name is only for linkers.
    cp "$runtime" "$root/usr/lib/sonicator/libonnxruntime.so.1"
    chmod 644 "$root/usr/lib/sonicator/libonnxruntime.so.1"
    ln -sf libonnxruntime.so.1 "$root/usr/lib/sonicator/libonnxruntime.so"
    if [ -f "$build/Sonicator_artefacts/Release/libonnxruntime_providers_shared.so" ]; then
        cp "$build/Sonicator_artefacts/Release/libonnxruntime_providers_shared.so" \
            "$root/usr/lib/sonicator/"
    fi
    cp "$here/sonicator.desktop" "$root/usr/share/applications/sonicator.desktop"
    cp "$here/README.txt" "$root/usr/share/doc/sonicator/README.txt"
    for size in 16 32 48 64 128 256 512; do
        directory="$root/usr/share/icons/hicolor/${size}x${size}/apps"
        mkdir -p "$directory"
        cp "$here/icons/${size}x${size}.png" "$directory/sonicator.png"
    done
    verify_glibc "$root/usr/bin/SONICATOR"
    verify_app_rpath "$root/usr/bin/SONICATOR"
    verify_glibc "$root/usr/lib/sonicator/libonnxruntime.so.1"
    python3 "$repo/packaging/collect-licenses.py" "$build" "$root/usr/share/doc/sonicator/Licenses"
}

stage="$(mktemp -d "${TMPDIR:-/tmp}/sonicator-linux.XXXXXX")"
trap 'rm -rf "$stage"' EXIT HUP INT TERM

# --- Debian package -------------------------------------------------------------------------
deb_root="$stage/deb"
stage_tree "$deb_root"
installed_kb="$(du -sk "$deb_root/usr" | cut -f1)"
mkdir -p "$deb_root/DEBIAN"
cat > "$deb_root/DEBIAN/control" <<CONTROL
Package: sonicator
Version: $version
Section: sound
Priority: optional
Architecture: amd64
Installed-Size: $installed_kb
Maintainer: Ilya Orange <sonicator@sampledicer.pro>
Depends: libc6 (>= $glibc_baseline), libasound2, libfreetype6, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1, libgl1, libjack-jackd2-0 | pipewire-jack
Recommends: pipewire
Description: Intelligent sample explorer for the local audio library
 SONICATOR indexes, analyses and plays a local sample collection, keeps tags,
 key, BPM and genre in one SQLite catalog, and exports audio and prepared WAVs
 for drag and drop into a DAW.
CONTROL
dpkg-deb --root-owner-group --build "$deb_root" "$deb"
test -f "$deb"

# --- AppImage ------------------------------------------------------------------------------
appdir="$stage/SONICATOR.AppDir"
stage_tree "$appdir"
cp "$here/AppRun" "$appdir/AppRun"
chmod 755 "$appdir/AppRun"
ln -sf usr/share/applications/sonicator.desktop "$appdir/sonicator.desktop"
ln -sf usr/share/icons/hicolor/256x256/apps/sonicator.png "$appdir/sonicator.png"
# FUSE is not available in a container, so the tool unpacks itself instead of mounting.
ARCH=x86_64 "$appimagetool" --appimage-extract-and-run --no-appstream "$appdir" "$image"
test -f "$image"
chmod 755 "$image"

# Read the AppImage back: it must contain the payload, not a reference to the staging tree.
( cd "$stage" && "$image" --appimage-extract >/dev/null )
for expected in usr/bin/SONICATOR usr/lib/sonicator/libonnxruntime.so.1 \
                usr/share/applications/sonicator.desktop AppRun; do
    test -f "$stage/squashfs-root/$expected" || {
        echo "AppImage is missing $expected" >&2; exit 1; }
done

echo "Built $deb"
echo "Built $image"
( cd "$output" && sha256sum "$(basename "$deb")" "$(basename "$image")" > SONICATOR-"$version"-Linux.sha256 )
echo "Wrote $output/SONICATOR-$version-Linux.sha256"
