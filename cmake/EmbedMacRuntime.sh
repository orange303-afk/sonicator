#!/bin/sh
# Embed before sealing the bundle; no dependency on the original build directory remains.
set -eu
bundle="$1"
runtime="$2"
binary="$bundle/Contents/MacOS/$3"
test -f "$runtime" && test -f "$binary" || { echo "Missing bundle executable or runtime" >&2; exit 1; }
frameworks="$bundle/Contents/Frameworks"
mkdir -p "$frameworks"
/usr/bin/ditto --norsrc --noextattr "$runtime" "$frameworks/libonnxruntime.dylib"
old_id="$(/usr/bin/otool -D "$runtime" | tail -1)"
/usr/bin/install_name_tool -change "$old_id" '@rpath/libonnxruntime.dylib' "$binary"
if ! /usr/bin/otool -l "$binary" | grep -Fq 'path @loader_path/../Frameworks '; then
    /usr/bin/install_name_tool -add_rpath '@loader_path/../Frameworks' "$binary"
fi
/usr/bin/codesign --force --sign - "$frameworks/libonnxruntime.dylib"
/usr/bin/codesign --force --sign - "$bundle"
/usr/bin/codesign --verify --deep --strict "$bundle"
