# Linux release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship SONICATOR for Linux as an AppImage and a `.deb`, built in CI on the glibc 2.35 baseline, with the application suite green on Linux and an owner acceptance record.

**Architecture:** No new subsystem. The port is a platform branch in `CMakeLists.txt` (runtime rpath, JACK, Linux package post-build copy), one corrected platform string in the contribution path, a `packaging/linux/` tree that mirrors `packaging/macos`, a non-blocking CI job that becomes blocking once green, and documentation. Everything else is measurement: the Windows port already proved the tree compiles elsewhere without source edits.

**Tech Stack:** C++20, JUCE 9.0.5, CMake 3.22+, Ninja, GCC 13, Catch2, Xvfb, dpkg-deb, linuxdeploy (AppImage).

**Spec:** `docs/superpowers/specs/2026-11-17-linux-release-design.md`

## Global Constraints

- glibc floor for every shipped Linux binary: **2.35**. Release builds run inside `ubuntu:22.04`.
- Linux release artifacts are x86_64 only: `SONICATOR-<version>-Linux-x86_64.AppImage`,
  `SONICATOR-<version>-Linux-amd64.deb`, `SONICATOR-<version>-sources.tar.gz`, plus `SHA256`
  and `source-commit.txt`.
- `JUCE_ALSA=1` and `JUCE_JACK=1` in every Linux build.
- No new runtime dependency beyond what JUCE already links; the ONNX Runtime `.so` is shipped
  beside the binary and resolved with `$ORIGIN`.
- Contribution `os` value on Linux is `"linux"`, and `services/contribute-worker` must accept it.
- Public text — workflow and step names, docs, release notes, commit messages — is English.
- Build trees stay outside the repository and outside iCloud; packaging refuses to stage with less
  than 8 GiB free.
- No Python at runtime.

## Review Focus

- **A sample folder whose name differs from another only by case.** A user on Linux can have both
  `Drums/` and `drums/`; the catalog must keep them distinct rather than merging or dropping one.
- **A library path containing a backslash or a colon.** Legal on Linux, and it must not be treated
  as a separator or a volume prefix.
- **Copy from the library.** On Linux the clipboard carries newline-separated paths; the user must
  be able to paste them into a terminal or a file manager text field, and nothing must crash when
  the clipboard is empty.
- **Dragging a selection out while the drag cache is on another filesystem.** The prepared WAV is
  written under the export folder; a full or read-only home must produce a message, not silence.
- **An audio device that disappears mid-preview** (USB interface unplugged, PipeWire restarted).
  The player must stop cleanly and stay usable rather than wedging the audio thread.

---

### Task 1: Non-blocking Linux CI job and the port record

**Files:**
- Modify: `.github/workflows/ci.yml` (append a `linux` job after the `windows` job)
- Create: `docs/LINUX_PORT.md`

**Interfaces:**
- Consumes: nothing.
- Produces: the job name `linux`, the preset-free configure line
  `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`, and the file
  `docs/LINUX_PORT.md` whose "What works", "Failing tests" and "What is not done" sections later
  tasks append to.

- [ ] **Step 1: Add the job**

```yaml
  # Linux has never been built. This job is informational: it must not fail the run while the port
  # is being established. The state it reaches is recorded in docs/LINUX_PORT.md.
  linux:
    name: Linux Release (informational)
    runs-on: ubuntu-24.04
    container: ubuntu:22.04
    continue-on-error: true
    timeout-minutes: 90
    steps:
      - name: Install build dependencies
        run: |
          apt-get update
          DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            build-essential cmake ninja-build git ca-certificates \
            libasound2-dev libjack-jackd2-dev \
            libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev \
            libfreetype-dev libgl1-mesa-dev libglu1-mesa-dev \
            xvfb
      - uses: actions/checkout@v4
      - name: Cache fetched dependencies
        uses: actions/cache@v4
        with:
          path: build/_deps
          key: linux-deps-${{ hashFiles('CMakeLists.txt', 'tests/CMakeLists.txt', 'CMakePresets.json', 'cmake/RubberBand.cmake') }}
          restore-keys: |
            linux-deps-
      - name: Configure
        run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DJUCE_JACK=ON
      - name: Build
        run: cmake --build build -j 4
      - name: Test under Xvfb
        run: xvfb-run --auto-servernum build/tests/SonicatorTests
```

- [ ] **Step 2: Create `docs/LINUX_PORT.md`** with the sections `## What works`, `## Failing tests`,
  `## What is not done`, `## How to reproduce`, each holding the line "Not measured yet" where a
  value is unknown, and a first row recording `Configure` as `Unknown`.

- [ ] **Step 3: Run the job and record the result**

Run: `gh workflow run ci.yml --ref stage-linux-release` then `gh run watch`
Expected: the run is green overall even when the `linux` job fails (`continue-on-error`), and the
job log names every compile error and every failing test. Copy those lists into
`docs/LINUX_PORT.md`.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml docs/LINUX_PORT.md
git commit -m "ci: add an informational Linux build job and the port record"
```

---

### Task 2: Measure on the Ubuntu stand

**Files:**
- Modify: `docs/LINUX_PORT.md`

**Interfaces:**
- Consumes: Task 1's configure and build lines; the stand `ilyaorange@192.168.2.89` with build
  dependencies installed.
- Produces: the measured failure lists that Task 4 translates into fixes, and the build time figure
  recorded under `## What works`.

- [ ] **Step 1: Install the stand's dependencies** (needs the sudo password; the password is
  supplied through `openchamber_secret` and never written to a file or to the shell history)

Run: `ssh ilyaorange@192.168.2.89 'sudo -S apt-get install -y ...'` with the package list from
Task 1 Step 1 plus `libjack-jackd2-dev pipewire-jack`.
Expected: `dpkg -s` reports `Status: install ok installed` for `xvfb` and `libjack-jackd2-dev`.

- [ ] **Step 2: Sync the tree and configure**

Run: `rsync -az --delete --exclude .git --exclude graphify-out --exclude dist --exclude build ./ ilyaorange@192.168.2.89:Sonicator/`
then `ssh ilyaorange@192.168.2.89 'cd Sonicator && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON'`
Expected: configure finishes; every required package is found; any missing dependency is named.

- [ ] **Step 3: Build and record time**

Run: `ssh ilyaorange@192.168.2.89 'cd Sonicator && time cmake --build build -j 4'`
Expected: the build either finishes or stops with a named compiler error. Every error goes into
`docs/LINUX_PORT.md` under `## What works` with its file and line.

- [ ] **Step 4: Run the suite under Xvfb**

Run: `ssh ilyaorange@192.168.2.89 'cd Sonicator && xvfb-run --auto-servernum build/tests/SonicatorTests 2>&1 | tail -40'`
Expected: a Catch2 summary of the form `test cases: N | M passed | K failed`. Record N, M, K and
the full list of failing test names in `## Failing tests`.

- [ ] **Step 5: Commit**

```bash
git add docs/LINUX_PORT.md
git commit -m "docs: record the first Linux build and test measurements"
```

---

### Task 3: Report `"linux"` to the contribution endpoint

**Files:**
- Create: `core/contribute/PlatformName.h`, `core/contribute/PlatformName.cpp`
- Modify: `CMakeLists.txt:178-224` (add `core/contribute/PlatformName.cpp` to `SonicatorCore`)
- Modify: `ui/MainComponent.cpp:5564-5570` (replace the inline `osName` branch)
- Modify: `services/contribute-worker/src/index.ts:131`
- Test: `tests/TestContribution.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `juce::String sonicator::contribute::platformName()` returning `"mac"`, `"win"` or
  `"linux"`; the worker allowlist accepting exactly those three strings.

- [ ] **Step 1: Write the failing test** — append to `tests/TestContribution.cpp`:

```cpp
TEST_CASE ("The contribution platform name matches the host system", "[contribute]")
{
    const auto name = contribute::platformName();
    CHECK (name.isNotEmpty());
    CHECK (juce::StringArray ("mac", "win", "linux").contains (name));
   #if JUCE_LINUX
    CHECK (name == "linux");
   #endif
}
```

- [ ] **Step 2: Run it and see it fail**

Run: `cmake --build --preset debug --target SonicatorTests && ./build/tests/SonicatorTests "[contribute]"`
Expected: a compile error, because `platformName` does not exist.

- [ ] **Step 3: Implement it**

`core/contribute/PlatformName.h`:

```cpp
#pragma once
#include <juce_core/juce_core.h>
namespace sonicator::contribute {
/** The `os` field the contribution worker accepts: "mac", "win" or "linux". */
juce::String platformName();
}
```

`core/contribute/PlatformName.cpp` returns `"win"` under `JUCE_WINDOWS`, `"linux"` under
`JUCE_LINUX`, and `"mac"` otherwise.

- [ ] **Step 4: Use it in `ui/MainComponent.cpp`** — replace the
  `#if JUCE_WINDOWS / osName = "win" / #else / "mac"` block with
  `const auto osName = contribute::platformName();`.

- [ ] **Step 5: Accept it in the worker** — change
  `if (batch.os !== "mac" && batch.os !== "win") return "os";` to also allow `"linux"`, and add a
  case to the worker's own test file asserting a `linux` batch is accepted.

- [ ] **Step 6: Run both suites**

Run: `./build/tests/SonicatorTests "[contribute]"` and `cd services/contribute-worker && npm test`
Expected: PASS for both.

- [ ] **Step 7: Commit**

```bash
git add core/contribute/PlatformName.h core/contribute/PlatformName.cpp CMakeLists.txt \
        ui/MainComponent.cpp tests/TestContribution.cpp services/contribute-worker
git commit -m "fix: report linux as the contribution platform and accept it in the worker"
```

> Deployment note: the worker must be deployed (`npx wrangler deploy`) before or with the Linux
> release. Until then the worker rejects Linux batches with `os`, which the client already handles
> as a normal failed upload.

---

### Task 4: Make the application build and start on Linux

**Files:**
- Modify: `CMakeLists.txt:260-285` (Linux post-build copy and rpath)
- Modify: `plugin/CMakeLists.txt:38-46` (Linux VST3 rpath; keeps the plug-in buildable, not released)
- Modify: `docs/LINUX_PORT.md`

**Interfaces:**
- Consumes: Task 2's compile-error list.
- Produces: `build/Sonicator_artefacts/Release/SONICATOR` that starts under `xvfb-run` with
  `libonnxruntime.so` resolved through `$ORIGIN`.

- [ ] **Step 1: Fix each compile error from Task 2** one commit at a time, each commit naming the
  file and the platform reason.

- [ ] **Step 2: Add the Linux runtime layout** to `CMakeLists.txt` next to the `WIN32` block:

```cmake
if(UNIX AND NOT APPLE)
    set_target_properties(Sonicator PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN")
    add_custom_command(TARGET Sonicator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so" "$<TARGET_FILE_DIR:Sonicator>"
        VERBATIM)
endif()
```

- [ ] **Step 3: Verify the binary starts**

Run: `xvfb-run --auto-servernum build/Sonicator_artefacts/Release/SONICATOR &` then
`sleep 5 && ldd build/Sonicator_artefacts/Release/SONICATOR | grep "not found"`
Expected: no `not found` lines, and the process is still alive after 5 seconds (check with
`pgrep -f SONICATOR`). Kill it afterwards.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt plugin/CMakeLists.txt docs/LINUX_PORT.md
git commit -m "build: resolve the Linux runtime beside the binary and start the app headless"
```

---

### Task 5: Green suite on Linux

**Files:**
- Modify: `tests/*.cpp` (only the files that fail), `docs/LINUX_PORT.md`
- Modify: `.github/workflows/ci.yml` (drop `continue-on-error` once green)

**Interfaces:**
- Consumes: Task 2's failing-test list.
- Produces: `xvfb-run build/tests/SonicatorTests` exiting 0; the `linux` job required.

- [ ] **Step 1: Classify every failure** into one of: test assumed macOS paths, test compares pixel
  colours, product code assumed a case-insensitive filesystem, product code assumed macOS I/O.
  Append the classification table to `docs/LINUX_PORT.md`.

- [ ] **Step 2: Fix the macOS-path assumptions** by switching to `juce::File::getSpecialLocation`
  or the production helper under test; never by hard-coding a Linux path.

- [ ] **Step 3: Fix product-code defects** found in Step 1 with a regression test that fails on the
  old behaviour — for example, a test in `tests/TestDatabase.cpp` that inserts `Drums/a.wav` and
  `drums/a.wav` and expects `countInFolder` to keep them apart on Linux.

- [ ] **Step 4: Adjust pixel tolerances only where the rasterizer differs**, never by deleting an
  assertion.

- [ ] **Step 5: Run the whole suite three times**

Run: `xvfb-run --auto-servernum build/tests/SonicatorTests`
Expected: PASS three times in a row, with the same case count as macOS.

- [ ] **Step 6: Make the CI job required** by removing `continue-on-error: true`, and commit.

```bash
git add tests docs/LINUX_PORT.md .github/workflows/ci.yml
git commit -m "test: make the Linux suite green and required in CI"
```

---

### Task 6: Package AppImage and .deb

**Files:**
- Create: `packaging/linux/build-packages.sh`, `packaging/linux/sonicator.desktop`,
  `packaging/linux/README.txt`, `packaging/linux/icons/{16,32,48,64,128,256,512}.png`
- Test: `packaging/tests/test-release.py` (extend the existing release-artifact tests)

**Interfaces:**
- Consumes: the built application from Task 4; `packaging/collect-licenses.py`;
  `packaging/make-source-bundle.py`.
- Produces: `dist/SONICATOR-<version>-Linux-x86_64.AppImage` and
  `dist/SONICATOR-<version>-Linux-amd64.deb`, both containing `usr/bin/SONICATOR`,
  `usr/lib/sonicator/libonnxruntime.so`, the `.desktop` entry and hicolor icons.

- [ ] **Step 1: Commit the icon set**, generated once from `resources/branding/icon-mark-1024.png`
  with `sips -z N N ... --out packaging/linux/icons/NxN.png`, so the build needs no image tool.

- [ ] **Step 2: Write `build-packages.sh`** with the same contract as `packaging/macos/build-pkg.sh`:
  `build-packages.sh BUILD_DIR OUTPUT_DIR`, refusing to overwrite existing artifacts and refusing to
  stage with less than 8 GiB free. It stages an AppDir, copies the binary and the runtime with the
  rpath already set, runs `packaging/collect-licenses.py`, builds the `.deb` with `dpkg-deb`, and
  builds the AppImage with
  `linuxdeploy-x86_64.AppImage --appimage-extract-and-run --appdir ... --output appimage`.

- [ ] **Step 3: Add the checks** in the same script: `readelf -d` shows `RPATH $ORIGIN` and no build
  path; `objdump -T` on both binaries reports no `GLIBC_` version above `2.35`; the `.deb` control
  file lists the dependencies from the spec; the AppImage's `.desktop` name and icon resolve.

- [ ] **Step 4: Extend the packaging tests** with a case that fails when an artifact's glibc
  ceiling is above 2.35 and when the `.deb` `Depends:` list is missing `libasound2`.

- [ ] **Step 5: Run the packager on the stand**

Run: `ssh ilyaorange@192.168.2.89 'cd Sonicator && sh packaging/linux/build-packages.sh build ../../sonicator-dist'`
Expected: both artifacts exist, every check passes, and `dpkg-deb -I` prints the dependency list.

- [ ] **Step 6: Commit**

```bash
git add packaging/linux packaging/tests/test-release.py
git commit -m "feat: package the Linux application as AppImage and deb"
```

---

### Task 7: Publish Linux artifacts from the release workflow

**Files:**
- Modify: `.github/workflows/release-installers.yml` (add a `linux` job)

**Interfaces:**
- Consumes: `packaging/linux/build-packages.sh` and `packaging/make-source-bundle.py`.
- Produces: a workflow job named `Linux x86_64 (AppImage + deb)` uploading the two artifacts, the
  matching sources archive, `SHA256` files and `source-commit.txt`.

- [ ] **Step 1: Add the job** on `runs-on: ubuntu-24.04` with `container: ubuntu:22.04`, the
  dependency install from Task 1, `SONICATOR_BUILD_PLUGIN=OFF`, the packaging step, the checksum
  verification loop copied from the Windows job, and an upload step.

- [ ] **Step 2: Verify on a real tag** with workflow dispatch, then confirm on the downloaded
  artifacts that `objdump -T` stays at or below `GLIBC_2.35` and the AppImage runs.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release-installers.yml
git commit -m "ci: publish Linux AppImage and deb from the release workflow"
```

---

### Task 8: Document Linux and record acceptance

**Files:**
- Modify: `README.md` (badge, requirements, build section, path tables, clipboard note),
  `docs/DEPENDENCIES.md`, `docs/TESTING.md`, `docs/RELEASE_INSTALLERS.md`, `AGENTS.md`
- Create: `docs/LINUX_ACCEPTANCE.md`

**Interfaces:**
- Consumes: everything above.
- Produces: a README that names Linux beside macOS and Windows, a paths table with
  `~/.config/Sonicator/...`, and an acceptance file with the owner's results.

- [ ] **Step 1: Update the docs** with the measured facts only — no claim that a DAW import or a
  device switch was verified unless it was.

- [ ] **Step 2: Run the acceptance checklist** on the stand with its monitor and speakers, filling
  in `docs/LINUX_ACCEPTANCE.md`: `.deb` install, AppImage run, indexing, preview through PipeWire
  and JACK, device switch during playback, export, prepared-WAV drag into REAPER/Bitwig/Ardour,
  Cloud view, HiDPI, X11 and Wayland.

- [ ] **Step 3: Refresh the knowledge graph**

Run: `graphify update .`
Expected: the command completes and `graphify-out/` reflects the new files.

- [ ] **Step 4: Commit**

```bash
git add README.md docs AGENTS.md
git commit -m "docs: describe the Linux release and record its acceptance"
```
