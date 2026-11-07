#!/bin/sh
# Finder/FileProvider can attach FinderInfo and fileprovider attributes to a generated .app
# inside ~/Documents. codesign refuses such detritus, so sign a clean ditto copy and put the
# signed bundle back. The signature lives in _CodeSignature, not in extended attributes.
set -e
source_bundle="$1"
temporary="${TMPDIR:-/tmp}/sonicator-sign-$$.app"
rm -rf "$temporary"
/usr/bin/ditto --norsrc --noextattr --nocache "$source_bundle" "$temporary"
/usr/bin/codesign --force --sign - "$temporary"
rm -rf "$source_bundle"
/usr/bin/ditto --norsrc --noextattr --nocache "$temporary" "$source_bundle"
rm -rf "$temporary"
