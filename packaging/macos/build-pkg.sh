#!/bin/sh
# Package existing Release artifacts. Does not install or publish anything.
set -eu
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd -P)"
repo="$(CDPATH='' cd -- "$here/../.." && pwd -P)"
build="${1:?Usage: build-pkg.sh BUILD_DIR OUTPUT_DIR [VARIANT]}"
output="${2:?Usage: build-pkg.sh BUILD_DIR OUTPUT_DIR [VARIANT]}"
variant="${3:-universal}"
# The universal release package is the default. A legacy variant serves the Intel Macs that
# cannot install it: one architecture and a system floor the build itself was produced against.
case "$variant" in
    universal)
        expected_archs="arm64,x86_64"
        host_archs="arm64,x86_64"
        require_runtime=1
        min_os="11.0"
        # The bundled ONNX Runtime declares 13.3 of its own; the universal package carries that
        # floor until the next release drops the runtime. Only the legacy package is gated.
        max_minos=""
        file_suffix="macOS-universal" ;;
    legacy-x86_64)
        expected_archs="x86_64"
        host_archs="x86_64"
        require_runtime=0
        min_os="10.15"
        max_minos="10.15"
        file_suffix="macOS-x86_64-10.15" ;;
    *)
        echo "Unknown variant: $variant" >&2
        exit 1 ;;
esac
test -d "$build" || { echo "Missing build directory: $build" >&2; exit 1; }
version="$(sed -n 's/^project(Sonicator VERSION \([0-9][0-9.]*\) .*/\1/p' "$repo/CMakeLists.txt")"
test -n "$version"
app="$build/Sonicator_artefacts/Release/SONICATOR.app"
vst3="$build/plugin/SonicatorPlugin_artefacts/Release/VST3/SONICATOR.vst3"
au="$build/plugin/SonicatorPlugin_artefacts/Release/AU/SONICATOR.component"
for bundle in "$app" "$vst3" "$au"; do
    /bin/sh "$here/verify-bundle.sh" "$bundle" "$expected_archs" "$require_runtime" "$max_minos"
    actual="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$bundle/Contents/Info.plist")"
    test "$actual" = "$version" || { echo "Version mismatch: $bundle ($actual != $version)" >&2; exit 1; }
done
test -f "$vst3/Contents/Resources/moduleinfo.json"
available="$(df -Pk "$build" | awk 'NR==2 {print $4}')"
test "$available" -ge 8388608 || { echo 'Less than 8 GiB free; refusing to stage payloads' >&2; exit 1; }
stage="$(mktemp -d "${TMPDIR:-/tmp}/sonicator-pkg.XXXXXX")"
trap 'rm -rf "$stage"' EXIT HUP INT TERM
mkdir -p "$stage/packages" "$stage/resources"
cp "$here/README.txt" "$stage/resources/README.txt"
for component in app vst3 au; do
    root="$stage/$component/root"
    scripts="$stage/$component/scripts"
    case "$component" in
        app) source="$app"; relative='Applications/SONICATOR.app' ;;
        vst3) source="$vst3"; relative='Library/Audio/Plug-Ins/VST3/SONICATOR.vst3' ;;
        au) source="$au"; relative='Library/Audio/Plug-Ins/Components/SONICATOR.component' ;;
    esac
    target="$root/$relative"
    mkdir -p "$(dirname "$target")" "$scripts"
    /usr/bin/ditto --norsrc --noextattr "$source" "$target"
    # Each component is independently valid and carries its notices inside its own bundle.
    python3 "$repo/packaging/collect-licenses.py" "$build" "$target/Contents/Resources/Licenses"
    chmod 755 "$target/Contents/MacOS/SONICATOR"
    # A build without ONNX Runtime carries no nested dylib; nothing to sign before the bundle.
    if test -f "$target/Contents/Frameworks/libonnxruntime.dylib"; then
        /usr/bin/codesign --force --sign - "$target/Contents/Frameworks/libonnxruntime.dylib"
    fi
    /usr/bin/codesign --force --sign - "$target"
    /bin/sh "$here/verify-bundle.sh" "$target" "$expected_archs" "$require_runtime" "$max_minos"
    cp "$here/postinstall" "$scripts/postinstall"
    printf '%s\n' "$component" > "$scripts/component"
    chmod 755 "$scripts/postinstall"
    /usr/bin/pkgbuild --analyze --root "$root" "$stage/$component.plist"
    # pkgbuild recognizes .app bundles for relocation, not JUCE's plugin BNDL payloads.
    if test "$component" = app; then
        /usr/libexec/PlistBuddy -c 'Set :0:BundleIsRelocatable false' "$stage/$component.plist"
        /usr/libexec/PlistBuddy -c 'Set :0:BundleOverwriteAction upgrade' "$stage/$component.plist"
    fi
    /usr/bin/pkgbuild --root "$root" --component-plist "$stage/$component.plist" \
        --scripts "$scripts" --identifier "com.sonicator.installer.$component" \
        --version "$version" --install-location / --ownership recommended "$stage/packages/$component.pkg"
done
sed -e "s/@VERSION@/$version/g" -e "s/@MIN_OS@/$min_os/g" -e "s/@HOST_ARCHS@/$host_archs/g" \
    "$here/Distribution.xml.in" > "$stage/Distribution.xml"
mkdir -p "$output"
result="$output/SONICATOR-$version-$file_suffix.pkg"
test ! -e "$result" || { echo "Refusing to overwrite $result" >&2; exit 1; }
/usr/bin/productbuild --distribution "$stage/Distribution.xml" --package-path "$stage/packages" \
    --resources "$stage/resources" "$result"
(cd "$output" && /usr/bin/shasum -a 256 "$(basename "$result")" > "$(basename "$result").sha256")
echo "Created unsigned installer: $result"
