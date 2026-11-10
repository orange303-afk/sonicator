#!/bin/sh
# Makes a plugin bundle self-contained: copies a dynamic library into Contents/Frameworks and
# rewrites the plugin binary's load command so it refers to the copy, not to the build tree.
# The rewrite invalidates the linker signature, so signing must happen afterwards.
set -e
bundle="$1"
source_dylib="$2"
binary_name="$3"
frameworks="$bundle/Contents/Frameworks"
binary="$bundle/Contents/MacOS/$binary_name"

mkdir -p "$frameworks"
/usr/bin/ditto "$source_dylib" "$frameworks/libonnxruntime.dylib"
old_id="$(/usr/bin/otool -D "$source_dylib" | tail -1)"
/usr/bin/install_name_tool -change "$old_id" "@rpath/libonnxruntime.dylib" "$binary"
