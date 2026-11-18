#!/usr/bin/env python3
"""Matching source + pinned dependencies for rebuilding/relinking shipped binaries."""
import io
import pathlib
import re
import subprocess
import sys
import tarfile

repo = pathlib.Path(__file__).resolve().parents[1]
build, output = (pathlib.Path(p).resolve() for p in sys.argv[1:])
version = re.search(r"project\(Sonicator VERSION ([\d.]+)", (repo / "CMakeLists.txt").read_text())[1]
commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
# Archive exactly the committed tree. A dirty or line-ending-normalized working copy must
# never replace the source that produced the binaries, nor abort packaging (Windows CRLF).
committed = subprocess.run(["git", "archive", "--format=tar", "HEAD"], cwd=repo,
                           check=True, capture_output=True).stdout
sources = {name: build / "_deps" / f"{name}-src" for name in
           ("juce", "libxmp", "libsamplerate", "rubberband")}
# ONNX Runtime is distributed as a NuGet package, extracted into a versioned directory rather
# than a FetchContent `_deps` checkout.
onnx_sources = sorted(build.glob("onnxruntime-*-src"))
if not onnx_sources:
    sys.exit(f"Missing pinned dependency source: onnxruntime: {build}/onnxruntime-*-src")
sources["onnxruntime"] = onnx_sources[-1]
sources["lame"] = build / "lame-src"
zlib = build / "_deps/zlib-src"
if zlib.is_dir():
    sources["zlib"] = zlib
for name, path in sources.items():
    if not path.is_dir():
        sys.exit(f"Missing pinned dependency source: {name}: {path}")
output.mkdir(parents=True, exist_ok=True)
archive_path = output / f"SONICATOR-{version}-sources.tar.gz"
if archive_path.exists():
    sys.exit(f"Refusing to overwrite {archive_path}")


def exclude_git(info):
    if ".git" in pathlib.PurePosixPath(info.name).parts or "__pycache__" in pathlib.PurePosixPath(info.name).parts:
        return None
    return info


instructions = f"""# SONICATOR {version} source and relinking materials

Source commit: {commit}

`source/` contains the matching project. `dependencies/` contains pinned JUCE,
libxmp, LAME, libsamplerate, Rubber Band sources and ONNX's MIT runtime package.
Licences are preserved inside each dependency. No sample library is included.

To rebuild/relink against modified LGPL libraries, edit dependencies/libxmp or
dependencies/lame as desired, then use CMake >=3.22, C++20 and the normal platform
SDK (Xcode Command Line Tools on macOS or Visual Studio 2022 C++ x64 on Windows).
Copy dependencies/lame to <build>/lame-src before configure. Pass these CMake flags
alongside -DSONICATOR_BUILD_PLUGIN=ON -DBUILD_TESTING=OFF and Release configuration:

""" + "\n".join(f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}=<absolute-path>/dependencies/{name}"
                   for name in sources if name not in ("lame", "onnxruntime")) + """

Build targets Sonicator and SonicatorPlugin_VST3; on macOS also SonicatorPlugin_AU.
For macOS pass -DCMAKE_OSX_ARCHITECTURES=\"arm64;x86_64\". Use the package scripts
under source/packaging to redistribute your modified builds under the applicable
licences. No vendor signing key, authorization or paid service is needed to
replace libraries/rebuild. Ad-hoc macOS signatures are generated locally.
ONNX Runtime (MIT) ships as the Microsoft.ML.OnnxRuntime NuGet package, extracted
from <build>/onnxruntime-1.20.1.nupkg; its MIT licence is in dependencies/onnxruntime.
"""
with tarfile.open(archive_path, "w:gz") as archive:
    with tarfile.open(fileobj=io.BytesIO(committed), mode="r:") as source:
        for member in source.getmembers():
            member.name = "source/" + member.name
            archive.addfile(member, source.extractfile(member) if member.isfile() else None)
    for name, path in sources.items():
        archive.add(path, arcname="dependencies/" + name, filter=exclude_git)
    data = instructions.encode()
    info = tarfile.TarInfo("SOURCE_BUILD.md")
    info.size = len(data)
    archive.addfile(info, io.BytesIO(data))
import hashlib
# Chunked rather than hashlib.file_digest: the release container builds on the Python 3.10 that
# Ubuntu 22.04 ships, and the digest is not worth a newer interpreter.
hasher = hashlib.sha256()
with archive_path.open("rb") as stream:
    for chunk in iter(lambda: stream.read(1 << 20), b""):
        hasher.update(chunk)
digest = hasher.hexdigest()
archive_path.with_suffix(archive_path.suffix + ".sha256").write_text(
    f"{digest}  {archive_path.name}\n", newline="\n")
print(f"Created source/relink archive for {commit}: {archive_path}")
