# Dependencies and distribution

Verified against upstream sources on 2026-11-11, from the sources the build actually downloads.

The distribution inventory (what ships, in which version, under which licence) is
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). This file records why each dependency is present.
It is engineering guidance, not legal advice.

## Project licence

SONICATOR is free, non-commercial and open source under **AGPL-3.0-or-later**. That choice follows the
JUCE route below: JUCE is used under its open-source licence, and AGPL is the licence JUCE's open-source
option requires for a combined work.

## Build and runtime

- JUCE **9.0.5**, pinned tag (commit `d60b7a78030661574a615b586d9325ba7a7fa7dd`). Modules are **AGPLv3 or
  commercial**, not GPLv3. This build uses the open-source route; no commercial entitlement is assumed.
  Distributing a combined binary under AGPL requires the corresponding source and the applicable notices.
  Before any closed-source release the owner must select and satisfy the JUCE commercial EULA.
  - https://github.com/juce-framework/JUCE/blob/9.0.5/LICENSE.md
  - https://juce.com/legal/juce-9-licence/
  - Bundled codec and other third-party notices: JUCE's `JUCE.spdx.json` and module source notices in
    the fetched sources under `_deps/juce-src/`. Preserve these with distributions; the components this
    build compiles are listed in `THIRD_PARTY_NOTICES.md`.
- SQLite **3.47.2**, official amalgamation vendored in `resources/sqlite/`, public domain. No system
  SQLite dependency.
  - https://sqlite.org/copyright.html
  - https://sqlite.org/2024/sqlite-amalgamation-3470200.zip
- LAME **3.100**, **LGPL-2.1-or-later**, fetched as a pinned archive
  (`SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e`) and linked
  statically. Only the `libmp3lame` encoder sources are compiled (mpglib decoder, CLI and VBR
  frontend are not), so Batch Processing can write MP3 without an external executable. The POSIX
  configuration header is `cmake/lame-config.h`; MSVC uses LAME's `configMS.h`. Assertions are
  compiled out, as upstream does for the library.
  - https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
- Ogg Vorbis writing uses the **libvorbis/libogg code that JUCE 9 embeds** (see
  `juce_audio_formats/codecs/ogg`); no additional system library is required. Its licence is part of
  the JUCE distribution inventory above.
- libsamplerate **0.2.2**, **BSD-2-Clause**, fetched as a pinned archive
  (`SHA256=16e881487f184250deb4fcb60432d7556ab12cb58caea71ef23960aec6c0405a`) and linked
  statically. It provides the band-limited conversion used by Batch Processing. Examples, tests and
  install rules are off, so its test tree never joins Sonicator's ctest.
  - https://github.com/libsndfile/libsamplerate/blob/0.2.2/COPYING
- Rubber Band **3.3.0**, **GPL-2.0-or-later**, fetched as a pinned archive
  (`SHA256=2bb837fe00932442ca90e185af8a468f7591df0c002b4a9e27a1bced1563ac84`) and compiled as one
  static translation unit from `single/RubberBandSingle.cpp` by `cmake/RubberBand.cmake`. Only the
  plug-in's offline Match renderer uses it. The built-in FFT and resampler are selected on every
  platform, threading is off (the renderer already owns a worker), and no CLI, LADSPA, LV2, VAMP or
  JNI target is built. No system or Homebrew copy is picked up at build or run time.
  - https://github.com/breakfastquay/rubberband/blob/v3.3.0/COPYING
- Catch2 **3.7.1**, Boost Software License 1.0, test-only, fetched at configure time.
  - https://github.com/catchorg/Catch2/blob/v3.7.1/LICENSE.txt
- libxmp **4.6.2** (`libxmp-4.6.2` tag), **LGPL-2.1-or-later**, fetched at configure time and linked
  statically. It provides whole-module playback for the legacy tracker formats; SONICATOR's own readers
  extract source PCM. Depackers and the ProWizard loader are disabled (`LIBXMP_DISABLE_DEPACKERS`,
  `LIBXMP_DISABLE_PROWIZARD`), so a module is never expanded from an embedded archive and packed headers
  are not guessed. Debug builds compile the library without its `-DDEBUG` stderr tracing.
  - Why not libopenmpt: the pinned upstream tags do not ship a CMake or installable package target, so
    using it would require a hand-maintained source list or an external build step that cannot be made
    mandatory on Windows. libxmp is CMake-native, cross-platform and covers MOD/XM/S3M and related
    formats. Revisit if libopenmpt gains a supported CMake package.
  - Licence: LGPL-2.1-or-later is compatible with the project's AGPLv3 route. The combined binary must
    keep the usual source/notice obligations and allow relinking against a modified libxmp.
  - https://github.com/libxmp/libxmp/blob/libxmp-4.6.2/docs/COPYING
- ONNX Runtime **1.20.1**, MIT, fetched as the `Microsoft.ML.OnnxRuntime` NuGet package and taken from
  its per-platform native libraries (macOS arm64 + x64 combined with `lipo`, Windows x64, Linux x64).
  The package is the C/C++ install route in the ONNX Runtime documentation; the GitHub release assets
  are the same binaries but stopped answering on 2026-11-17.
  - https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime/1.20.1
  - https://onnxruntime.ai/docs/install/
  - **The runtime is linked but no model file ships with the product.** `core/ml/OnnxAudioClassifier`
    exposes `loadModel` and `loadModelFromMemory`, and `core/smart/AcousticClassifierService` only runs
    the classifier when a model has been loaded; nothing in the application calls either loader, so
    `isLoaded()` is false at runtime and acoustic classification comes from the DSP rules
    (`core/dsp/AcousticFeatures`, `FastAcousticRules`) and the user-adaptive prototypes. The library
    still adds tens of megabytes to every bundle. Whether to drop it, ship a model, or keep it for a
    future stage is an open owner decision, recorded in `docs/BACKLOG.md`.
- zlib, found with `find_package(ZLIB)`, used by JUCE and by the NKI reader
  (`core/sampler/FileInstrument.cpp`, zlib-compressed XML). macOS takes the copy in the Command Line
  Tools SDK; Windows has none, so `CMakeLists.txt` fetches zlib 1.3.1 and links the static target
  instead of requiring vcpkg or a manual install.
- CMake >=3.22 and a C++20 compiler. Build presets are in `CMakePresets.json`.

No Python, HNSW, FAISS or UMAP runtime dependencies exist. The application makes no network requests:
`JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0` are set on `SonicatorCore`, and no component opens a socket.

WAV/AIFF/FLAC/MP3/OGG decoding is provided by `juce_audio_formats` (its bundled FLAC, Ogg Vorbis and Opus
codecs). MP3 decoding is explicitly enabled for consistent macOS/Windows behaviour. No FFmpeg dependency
is introduced.

# REX2 decoder reference

The DWOP decoder in `core/rex/Rex2Decoder.cpp` is adapted from
[VelociLoops](https://github.com/kunitoki/VelociLoops), whose LICENSE is the
Unlicense (public-domain dedication and warranty disclaimer), not MIT.
Reference source: `src/velociloops.cpp`; verified license:
https://github.com/kunitoki/VelociLoops/blob/main/LICENSE.
