#!/bin/sh
# Finder/FileProvider can attach FinderInfo and fileprovider attributes to a generated .app
# inside ~/Documents. Clear them and sign in one shell so the file provider cannot reattach
# them between the two operations.
set -e
/usr/bin/xattr -cr "$1"
exec /usr/bin/codesign --force --sign - "$1"
