#!/usr/bin/env python3
"""Collect pinned dependency notices into a payload (packaging-time only)."""
import pathlib
import shutil
import sys

repo = pathlib.Path(__file__).resolve().parents[1]
build, output = map(pathlib.Path, sys.argv[1:])
# The ONNX Runtime NuGet package is extracted into a versioned directory; the newest one is the
# runtime this build used.
onnx_sources = sorted(build.glob("onnxruntime-*-src"))
if not onnx_sources:
    sys.exit(f"Missing licence material: {build}/onnxruntime-*-src")
onnx_source = onnx_sources[-1]
files = {
    repo / "LICENSE": "SONICATOR-AGPL.txt",
    repo / "THIRD_PARTY_NOTICES.md": "THIRD_PARTY_NOTICES.md",
    repo / "docs/DATA_CREDITS.md": "DATA_CREDITS.md",
    repo / "resources/fonts/OFL.txt": "Manrope-OFL.txt",
    build / "lame-src/COPYING": "LAME-COPYING.txt",
    build / "_deps/libxmp-src/docs/COPYING": "libxmp-COPYING.txt",
    build / "_deps/libsamplerate-src/COPYING": "libsamplerate-COPYING.txt",
    build / "_deps/rubberband-src/COPYING": "RubberBand-COPYING.txt",
    onnx_source / "LICENSE": "ONNX-LICENSE.txt",
    onnx_source / "ThirdPartyNotices.txt": "ONNX-ThirdPartyNotices.txt",
    build / "_deps/juce-src/LICENSE.md": "JUCE/LICENSE.md",
    build / "_deps/juce-src/JUCE.spdx.json": "JUCE/JUCE.spdx.json",
}
# Include nested codec/SDK/graphics notices, preserving their source paths. Some licences
# (JPEG, FLAC etc.) live in headers/readmes, so retain those too, not just LICENSE files.
modules = build / "_deps/juce-src/modules"
for path in modules.rglob("*"):
    if path.is_file() and (path.name.lower().startswith(("license", "copying", "copyright", "readme"))
                           or path.suffix.lower() in {".h", ".hpp"}):
        files[path] = "JUCE/modules/" + path.relative_to(modules).as_posix()
zlib = build / "_deps/zlib-src/LICENSE"
if zlib.exists():
    files[zlib] = "zlib-LICENSE.txt"
for source in files:
    if not source.is_file():
        sys.exit(f"Missing licence material: {source}")
if output.exists():
    sys.exit(f"Licence destination already exists: {output}")
for source, name in files.items():
    destination = output / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
(output / "SOURCE.txt").write_text(
    "SONICATOR is AGPL-3.0-or-later. Source: https://github.com/orange303-afk/sonicator\n"
    "Distribute the matching SONICATOR-<version>-sources.tar.gz alongside the installer.\n"
    "It includes the application and pinned dependency sources so recipients can rebuild\n"
    "and relink against modified LGPL libraries (LAME and libxmp). See SOURCE_BUILD.md\n"
    "inside that archive. No Developer ID approval or vendor key is required to rebuild.\n",
    encoding="utf-8")
print(f"Collected {len(files)} licence/notice files into {output}")
