#!/bin/sh
# Inspect and exercise an actual PKG without installing over the owner's components.
set -eu
package="${1:?Usage: test-macos-pkg.sh INSTALLER.pkg}"
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd -P)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/sonicator-pkg-test.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
/usr/sbin/pkgutil --expand-full "$package" "$temporary/expanded"
python3 - "$temporary/expanded" <<'PY'
import pathlib
import sys
import xml.etree.ElementTree as ET
root = pathlib.Path(sys.argv[1])
distribution = ET.parse(root / "Distribution")
choices = distribution.findall("choice")
assert {c.attrib["id"] for c in choices} == {"app", "vst3", "au"}
assert all(c.attrib["start_selected"] == "true" for c in choices)
for component in ("app", "vst3", "au"):
    info = ET.parse(root / (component + ".pkg") / "PackageInfo")
    assert info.getroot().attrib["relocatable"] == "false"
    assert info.getroot().attrib["install-location"] == "/"
    assert len(info.findall("bundle")) == 1
    assert len(info.findall("relocate/bundle")) == 0
    assert (root / (component + ".pkg") / "Scripts/component").read_text().strip() == component
PY
for component in app vst3 au; do
    root="$temporary/expanded/$component.pkg/Payload"
    scripts="$temporary/expanded/$component.pkg/Scripts"
    case "$component" in
        app) bundle="$root/Applications/SONICATOR.app" ;;
        vst3) bundle="$root/Library/Audio/Plug-Ins/VST3/SONICATOR.vst3" ;;
        au) bundle="$root/Library/Audio/Plug-Ins/Components/SONICATOR.component" ;;
    esac
    # macOS /var is a symlink: pass the canonical volume path, as Installer does for /.
    root="$(CDPATH='' cd -- "$root" && pwd -P)"
    /bin/sh "$here/../macos/verify-bundle.sh" "$bundle"
    /usr/bin/xattr -w com.apple.quarantine '0081;test;test;' "$bundle"
    /bin/sh "$scripts/postinstall" "$package" / "$root"
    if /usr/bin/xattr -r "$bundle" | grep -q com.apple.quarantine; then exit 1; fi
    /bin/sh "$scripts/postinstall" "$package" / "$root"
    /usr/bin/codesign --verify --deep --strict "$bundle"
    test -f "$bundle/Contents/Resources/Licenses/SONICATOR-AGPL.txt"
done
echo 'PASS: product choices, fixed payload locations, universal signatures, licences and packaged postinstall scripts'
