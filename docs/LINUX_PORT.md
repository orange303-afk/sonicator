# Linux build: current state

SONICATOR is built for Linux by CI. This file records how far the port has reached, so the work can
continue without re-discovering it. A green Linux job is not claimed until the job stops being
informational.

The first measurement was taken on the owner's Ubuntu stand (see *How to reproduce*) on 2026-11-17,
before any source change; CI runs afterwards use the `ci-linux` preset in the same container.

## What works

| Step | State |
|---|---|
| Configure (`ci-linux`, Ninja, Release, JACK) | Passed in the `ubuntu:22.04` container after two fixes: the FreeType headers are `libfreetype6-dev` on jammy, and `DOWNLOAD_EXTRACT_TIMESTAMP` is a CMake 3.24 keyword that 3.22 reads as a second `URL_HASH` value |
| Fetch dependencies (JUCE, ONNX Runtime linux-x64, libxmp, LAME, libsamplerate, Rubber Band, Catch2) | Passed |
| `SonicatorCore`, `SonicatorUI`, `SONICATOR` | Built |
| `SonicatorTests` | Built |
| Packaging behavior tests (`packaging/tests/test-linux-package.sh`) | Passed |
| Full suite on the stand (Ubuntu 24.04, glibc 2.39) | **1283 cases: 1279 passed, 4 skipped, 0 failed**; 268315 assertions, 39 s |
| Full suite in the release container (Ubuntu 22.04, root) | Same suite; two tests skip under root because directory mode bits deny nothing to it |
| Full build time on the stand (4-core Celeron N4100) | 19 min 20 s from a warm configure |
| Application starts headless | Window opens under Xvfb, the process survives, and `~/.config/Sonicator/sonicator.sqlite` is created on first launch |
| Runtime loading | The archive's SONAME is `libonnxruntime.so.1`; the build now ships the real file under that name plus the unversioned symlink. Copying only the symlink made the application die with `libonnxruntime.so.1: cannot open shared object file` |

Source changes the port needed so far — every one a real defect, none a workaround:

- `core/scanner/FileScanner.cpp`: `juce::InputStream` overrides returned `int64_t`, which is `long` on
  glibc while JUCE returns `long long`. The type now comes from JUCE (`juce::int64`).
- `core/dsp/AnalysisAudio.cpp`: `std::min({a, b, c})` mixed `int64_t` with `juce::int64`; the
  template argument is explicit now.
- `core/ableton/AbletonPacks.cpp`: the same mismatch between a declared AIFC length and
  `getTotalLength()` in `juce::jmin`.
- `tests/TestCloudBuild.cpp`: `RelativeTime::milliseconds(int64_t)` was ambiguous on glibc.
- `core/contribute/PlatformName.cpp`: Linux used to report itself as `"mac"` to the contribution
  intake; it now reports `"linux"`, and `services/contribute-worker` accepts it.
- `CMakeLists.txt`: `JUCE_JACK` is not a CMake option — JUCE ignores it. JACK is compiled in with a
  compile definition and linked explicitly; the runtime is resolved through `$ORIGIN`.

## Failing tests

None. The four that failed during the port were fixed rather than tolerated:

| Test | Cause | Resolution |
|---|---|---|
| `A failed overwrite leaves the original byte-identical` | CI runs as root, and root is not stopped by a read-only directory | Skips under root, runs normally elsewhere |
| `An unwritable destination fails without touching the source or old output` | Same root bypass | Same skip |
| `Command and Control stay distinct` | Windows and Linux have one modifier; JUCE maps Command onto Control | The stored model stays distinct; the key press equality is asserted per platform |
| `MP3 output is a readable lossy copy...` (VBR half) | The encoder never replaced LAME's placeholder Xing frame, so a VBR file carried no frame count and its duration was guessed from the first frame: Linux read 2 s as 26496 samples | The encoder now writes the finished tag at the ID3 offset, and the test pins the `Xing` frame |

## What is not done

- The `linux` job in `.github/workflows/ci.yml` is informational (`continue-on-error`) until the
  suite is green.
- No Linux audio-device or MIDI acceptance: the suite runs without a device under Xvfb.
- No Linux artifacts have been produced yet: `packaging/linux/build-packages.sh` refuses to package a
  binary above the glibc 2.35 baseline, and the stand runs 24.04, so the first artifacts come from the
  release workflow's `ubuntu:22.04` container.
- `docs/TESTING.md` keeps Linux as unverified until the suite is green and acceptance is recorded.

## How to reproduce

CI runs the `linux` job in `.github/workflows/ci.yml`: `ubuntu:22.04` container, dependencies
installed with apt, `cmake --preset ci-linux`, `cmake --build --preset ci-linux`,
`xvfb-run --auto-servernum ctest --preset ci-linux`.

The owner's stand is `ilyaorange@192.168.2.89`, Ubuntu 24.04.5, glibc 2.39, 4-core Celeron N4100,
7.6 GiB RAM. It is a newer glibc than the release baseline on purpose: it proves the sources build
on a current distro as well, and it is the acceptance machine. Dependencies:

```
sudo apt-get install -y --no-install-recommends build-essential cmake ninja-build git \
  libasound2-dev libjack-jackd2-dev ladspa-sdk \
  libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev \
  libxrender-dev libxi-dev libfreetype6-dev libfontconfig1-dev \
  libgl1-mesa-dev libglu1-mesa-dev libegl-dev xvfb pipewire-jack
```

Ubuntu 24.04 names the FreeType headers `libfreetype-dev` and pulls the rest through newer
transitions; 22.04, the release baseline, needs `libfreetype6-dev`. A missing `ft2build.h` during
configure means the FreeType headers are absent, not that JUCE is broken.
