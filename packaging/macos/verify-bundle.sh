#!/bin/sh
set -eu
bundle="$1"
test -d "$bundle" && test ! -L "$bundle" || { echo "Missing bundle: $bundle" >&2; exit 1; }
test -z "$(find "$bundle" -type l -print)" || { echo "Symlinks in payload: $bundle" >&2; exit 1; }
plist="$bundle/Contents/Info.plist"
name="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")"
binary="$bundle/Contents/MacOS/$name"
runtime="$bundle/Contents/Frameworks/libonnxruntime.dylib"
test -x "$binary" && test -f "$runtime" || { echo "Missing executable/runtime: $bundle" >&2; exit 1; }
for file in "$binary" "$runtime"; do
    architectures="$(/usr/bin/lipo -archs "$file")"
    echo "$architectures" | grep -qw arm64
    echo "$architectures" | grep -qw x86_64
    # All non-system absolute dependencies must have been embedded before packaging.
    # Universal otool emits a separate filename header for each architecture.
    /usr/bin/otool -L "$file" | sed -n 's/^[[:space:]]*\(.*\) (compatibility version.*/\1/p' | while IFS= read -r dependency; do
        case "$dependency" in
            @rpath/*|@loader_path/*|@executable_path/*|/System/Library/*|/usr/lib/*) ;;
            *) echo "Non-relocatable dependency: $dependency ($file)" >&2; exit 1 ;;
        esac
    done
done
/usr/bin/otool -L "$binary" | grep -Fq '@rpath/libonnxruntime.dylib'
/usr/bin/codesign --verify --deep --strict "$bundle"
