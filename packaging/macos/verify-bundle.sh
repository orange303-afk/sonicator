#!/bin/sh
# Verify a packaged bundle: real directory, architecture set, relocatable dependencies, minimum
# system version and an ad-hoc signature that covers every nested item.
# Usage: verify-bundle.sh BUNDLE [ARCHS] [REQUIRE_RUNTIME] [MAX_MINOS]
#   ARCHS           comma separated, default "arm64,x86_64"
#   REQUIRE_RUNTIME 1 to demand the bundled ONNX Runtime, 0 when the build has none (default 1)
#   MAX_MINOS       highest acceptable LC_BUILD_VERSION minos, empty to skip the check
set -eu
bundle="$1"
archs_csv="${2:-arm64,x86_64}"
require_runtime="${3:-1}"
max_minos="${4:-}"
test -d "$bundle" && test ! -L "$bundle" || { echo "Missing bundle: $bundle" >&2; exit 1; }
test -z "$(find "$bundle" -type l -print)" || { echo "Symlinks in payload: $bundle" >&2; exit 1; }
plist="$bundle/Contents/Info.plist"
name="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")"
binary="$bundle/Contents/MacOS/$name"
runtime="$bundle/Contents/Frameworks/libonnxruntime.dylib"
test -x "$binary" || { echo "Missing executable: $bundle" >&2; exit 1; }
if test "$require_runtime" = 1; then
    test -f "$runtime" || { echo "Missing bundled runtime: $bundle" >&2; exit 1; }
fi

check_file() {
    file="$1"
    architectures="$(/usr/bin/lipo -archs "$file")"
    for arch in $(printf '%s' "$archs_csv" | tr ',' ' '); do
        echo "$architectures" | grep -qw "$arch" ||
            { echo "Missing architecture $arch: $file ($architectures)" >&2; exit 1; }
    done
    # A bundle that must stay single-architecture may not carry a slice it never asked for.
    case ",$archs_csv," in
        *,arm64,*) ;;
        *) echo "$architectures" | grep -qw arm64 &&
               { echo "Unexpected arm64 slice: $file" >&2; exit 1; } ;;
    esac
    /usr/bin/otool -l "$file" | sed -n 's/^[[:space:]]*path \(.*\) (offset.*/\1/p' | while IFS= read -r path; do
        case "$path" in
            '@loader_path/../Frameworks'|'@loader_path') ;;
            *) echo "Non-relocatable rpath: $path ($file)" >&2; exit 1 ;;
        esac
    done
    # All non-system absolute dependencies must have been embedded before packaging.
    # Universal otool emits a separate filename header for each architecture.
    /usr/bin/otool -L "$file" | sed -n 's/^[[:space:]]*\(.*\) (compatibility version.*/\1/p' | while IFS= read -r dependency; do
        case "$dependency" in
            @rpath/*|@loader_path/*|@executable_path/*|/System/Library/*|/usr/lib/*) ;;
            *) echo "Non-relocatable dependency: $dependency ($file)" >&2; exit 1 ;;
        esac
    done
    if test -n "$max_minos"; then
        /usr/bin/otool -l "$file" | awk -v max="$max_minos" -v file="$file" '
            $1 == "minos" {
                have = $2
                split(have, a, ".")
                split(max, b, ".")
                for (i = 1; i <= 3; i++) {
                    av = a[i] + 0
                    bv = b[i] + 0
                    if (av > bv) {
                        printf "minos %s exceeds %s: %s\n", have, max, file > "/dev/stderr"
                        bad = 1
                        break
                    }
                    if (av < bv) break
                }
            }
            END { exit bad }'
    fi
}

check_file "$binary"
if test -f "$runtime"; then
    check_file "$runtime"
fi
if test "$require_runtime" = 1; then
    /usr/bin/otool -L "$binary" | grep -Fq '@rpath/libonnxruntime.dylib'
fi
/usr/bin/codesign --verify --deep --strict "$bundle"
