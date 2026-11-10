#!/bin/sh
# Codesign a plugin bundle for local use. Nested dylibs must be signed before the enclosing
# bundle, otherwise macOS rejects the structure ("code object is not signed at all").
# This is an ad-hoc development signature, not Developer ID signing/notarization.
set -e
bundle="$1"
nested="$2"
if [ -n "$nested" ] && [ -e "$nested" ]; then
    /usr/bin/codesign --force --sign - "$nested"
fi
/usr/bin/codesign --force --sign - "$bundle"
