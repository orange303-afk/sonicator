# Dependencies and distribution

Verified directly against upstream on 2026-11-06.

- JUCE **9.0.5**, pinned tag (commit `d60b7a78030661574a615b586d9325ba7a7fa7dd`). Modules are **AGPLv3 or commercial**, not GPLv3. This development build uses the open-source route; no commercial entitlement is assumed. Distributing a combined binary under AGPL requires corresponding source and applicable notices. Before a closed-source release the owner must select and satisfy the JUCE commercial EULA. Starter is free up to the EULA's $20,000 revenue/funding limit; eligibility depends on the owner's circumstances, not just product sales. No subscription or purchase has been made by this project.
  - https://github.com/juce-framework/JUCE/blob/9.0.5/LICENSE.md
  - https://juce.com/legal/juce-9-licence/
  - Bundled codec and other third-party notices: JUCE's `JUCE.spdx.json` and module source notices in `build/_deps/juce-src/`. Preserve these with distributions.
- SQLite **3.47.2**, official amalgamation vendored in `resources/sqlite/`, public domain. No system SQLite dependency.
  - https://sqlite.org/copyright.html
  - https://sqlite.org/2024/sqlite-amalgamation-3470200.zip
- Catch2 **3.7.1**, Boost Software License 1.0, test-only, fetched at configure time.
  - https://github.com/catchorg/Catch2/blob/v3.7.1/LICENSE.txt
- CMake >=3.22, C++20 compiler, platform audio/GUI SDKs. No Python, ONNX, HNSW, FAISS, UMAP or runtime network dependencies in Phase 1.

WAV/AIFF/FLAC/MP3 decoding is provided by `juce_audio_formats`. MP3 decoding is explicitly enabled for consistent macOS/Windows behavior. No FFmpeg dependency is introduced.

This inventory is engineering guidance, not legal advice. SONICATOR's own final distribution licence remains an owner decision; do not ship closed-source binaries merely because this development build succeeds.
# REX2 decoder reference

The DWOP decoder in `core/rex/Rex2Decoder.cpp` is adapted from
[VelociLoops](https://github.com/kunitoki/VelociLoops), whose LICENSE is the
Unlicense (public-domain dedication and warranty disclaimer), not MIT.
Reference source: `src/velociloops.cpp`; verified license:
https://github.com/kunitoki/VelociLoops/blob/main/LICENSE.
