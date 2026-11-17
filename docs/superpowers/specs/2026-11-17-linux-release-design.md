# Linux release: design

Status: approved in conversation 2026-11-17; implementation follows
`docs/superpowers/plans/2026-11-17-linux-release.md`.

## Why

SONICATOR ships for macOS and Windows. The application is JUCE-based, the interface is drawn by
the app itself, the catalog is a shared SQLite file, and every dependency in `CMakeLists.txt`
already has a POSIX or Linux branch. A Linux build therefore extends the audience of a free
AGPL sample explorer to the Linux audio world (Bitwig, REAPER, Ardour, Carla users) at a cost
measured in days, not weeks: the Windows port compiled the tree without a single source edit.

## What ships

| Artifact | Contents |
|---|---|
| `SONICATOR-<version>-Linux-x86_64.AppImage` | the application, portable, no root needed |
| `SONICATOR-<version>-Linux-amd64.deb` | the application, native install for Ubuntu/Debian/Mint |
| `SONICATOR-<version>-sources.tar.gz` | matching sources, as macOS and Windows already ship |

Plus `SHA256` checksums, `source-commit.txt`, and a `docs/LINUX_ACCEPTANCE.md` record of what the
owner ran on a real machine.

## Decisions

### glibc baseline 2.35, built in a container

An AppImage does not carry glibc, so the build host's glibc is the runtime floor. Release
artifacts are built by CI inside `ubuntu:22.04` (glibc 2.35), which covers Ubuntu 22.04+, Debian
12+, Mint 21+, Fedora 36+. The application is never built on a newer distro for release.

Verification is part of packaging: every `GLIBC_` version referenced by the shipped binaries is
read with `objdump -T` and must be `<= 2.35`. This replaces the `otool` checks macOS uses.

### AppImage and .deb, x86_64 only

AppImage first, because it runs anywhere without root and is what Linux audio users expect for a
non-commercial tool. `.deb` second, because apt users prefer a package with dependencies declared
rather than an embedded runtime they cannot update.

`.deb` `Depends:` lists the libraries JUCE links at runtime: `libasound2`, `libfreetype6`,
`libx11-6`, `libxext6`, `libxrandr2`, `libxinerama1`, `libxcursor1`, `libgl1`.
`Recommends: pipewire-jack | libjack-jackd2-0`, so JACK users get the backend without forcing it
on everybody.

ARM64 is out of scope for this stage: ONNX Runtime publishes an aarch64 archive, but nothing else
in this design has been tested there, and no ARM machine is available for acceptance.

### Audio backends: ALSA plus JACK

`JUCE_ALSA=1` and `JUCE_JACK=1` on Linux. PipeWire users reach the engine through its
`pipewire-jack` compatibility layer or through ALSA, and JACK users get the native client.
Acceptance covers PulseAudio/PipeWire, `pipewire-jack` and classic JACK, plus device switching
while a preview is playing.

### Paths stay where JUCE puts them

`userApplicationDataDirectory` on Linux is `~/.config`, so the catalog lives in
`~/.config/Sonicator/sonicator.sqlite`, exports in `~/.config/Sonicator/exports`, and the log in
`~/.config/Sonicator/logs/sonicator.log` (the existing non-macOS branch of
`core/diagnostics/Diagnostics.cpp`). No new path abstraction is introduced; the shared catalog
format and the plugin compatibility rules stay as they are.

### Contribution platform string

The application reported `"mac"` for every non-Windows platform, which would have described Linux
users as macOS users in the training-data payload. The client sends `"linux"`, and
`services/contribute-worker/src/index.ts` accepts it in its allowlist. The worker must be deployed
before or together with the Linux release, or the worker rejects Linux batches with `os`.

### Desktop integration

`packaging/linux/` produces a `.desktop` entry (`Name=SONICATOR`, `Categories=AudioVideo;Audio;`,
`StartupWMClass=SONICATOR`) and hicolor icons at 16–512 px generated from
`resources/branding/icon-mark-1024.png`, which already exists for platform bundling.

### Clipboard and drag

External file drag uses JUCE's XDND path, the same code the other platforms use; the drag cache and
the prepared-WAV behaviour are unchanged. Copy on Linux writes newline-separated paths, as the
README already states for non-macOS platforms: a native file clipboard
(`x-special/gnome-copied-files`, `text/uri-list`) is a separate, optional follow-up, not part of
the release gate.

## Non-goals

- The VST3 plug-in for Linux. It needs a manifest step, `$ORIGIN` rpath, `~/.vst3` install rules and
  host acceptance in Bitwig/REAPER/Ardour. It is a later stage with its own spec.
- Flatpak or Snap: sandboxing fights the shared catalog with the plug-in and arbitrary sample
  folders, and neither is required to ship.
- Wayland-native support. JUCE runs through XWayland; the acceptance checklist records what the
  owner sees in both session types and no Wayland backend is written in this stage.
- Signing, notarization-equivalents, or repository hosting (PPA/AUR/Flathub).
- A native file clipboard, MIME associations for every exotic sample format, and a `.desktop`
  handler for disc images.

## Constraints

- No Python at runtime; packaging scripts may use it, as `make-source-bundle.py` already does.
- Dependencies stay pinned by the existing FetchContent declarations; no distro package replaces a
  pinned version.
- AGPL-3.0-or-later plus libxmp's LGPL mean the source archive ships with the binary, and the
  bundled `libonnxruntime.so` keeps its licence text via `packaging/collect-licenses.py`.
- Build trees live outside the repository and outside iCloud; staging needs ~8 GiB free, as the
  macOS packager already enforces.
- Public text (release notes, docs, commit messages, workflow names) is English.

## Risks

- **Case-sensitive filesystem.** macOS is case-insensitive by default, Linux is not. Path-scoped
  tests and duplicate detection are the likeliest new failures. The Windows port lost 20 tests to
  path separators alone; Linux keeps `/`, so that class disappears but this one may appear.
- **FreeType rasterizer.** Pixel assertions (`getPixelAt` in 11 tests) may differ. Windows already
  lost one such test to font metrics.
- **Headless CI.** Around 20 test files open real windows, so the suite needs `xvfb-run`; a missing
  display must fail loudly rather than hang.
- **XDND acceptance.** Whether a given DAW accepts JUCE's drag payload cannot be tested in CI; it
  is owner acceptance only.
- **Slow build host.** The acceptance box is a 4-core Celeron; a full build is 30–60 minutes, so
  iteration belongs in CI and on this Mac where possible.

## Acceptance

`docs/LINUX_ACCEPTANCE.md` records, with the owner's own results: install from `.deb` and run the
AppImage; first launch creates the catalog; a real folder tree indexes; preview plays through
PipeWire and through JACK; device switching during playback; selection export and prepared-WAV
drag into REAPER, Bitwig and Ardour; export folder and log paths; Cloud view; HiDPI at 125–200 %;
X11 and Wayland sessions; workspace look against the macOS screenshots.

## Left to the builder

Exact CI job layout, the packaging script structure, whether the hicolor icons are generated with
JUCE or with Python, and the order in which test failures are fixed. None of these changes what
ships.
