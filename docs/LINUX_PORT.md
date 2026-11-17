# Linux build: current state

SONICATOR is built for Linux by CI. This file records how far the port has reached, so the work can
continue without re-discovering it. A green Linux job is not claimed until the job stops being
informational.

The first measurement was taken on the owner's Ubuntu stand (see *How to reproduce*) on 2026-11-17,
before any source change; CI runs afterwards use the `ci-linux` preset in the same container.

## What works

| Step | State |
|---|---|
| Configure (`ci-linux`, Ninja, Release, JACK) | Not measured yet |
| Fetch dependencies (JUCE, ONNX Runtime linux-x64, libxmp, LAME, libsamplerate, Rubber Band, Catch2) | Not measured yet |
| `SonicatorCore`, `SonicatorUI`, `SonicatorTests`, `SONICATOR` | Not measured yet |
| `libonnxruntime.so` beside the binary, resolved with `$ORIGIN` | Not measured yet |
| Full build time on the stand (4-core Celeron N4100) | Not measured yet |

## Failing tests

Not measured yet.

## What is not done

- The `linux` job in `.github/workflows/ci.yml` is informational (`continue-on-error`) until the
  suite is green.
- No Linux audio-device or MIDI acceptance: the suite runs without a device under Xvfb.
- No installer: `packaging/linux/` and the release workflow follow the green suite.
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
  libasound2-dev libjack-jackd2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxcomposite-dev libfreetype-dev libgl1-mesa-dev libglu1-mesa-dev xvfb pipewire-jack
```
