# Windows port: green build, tests and VST3 — implementation plan

Date: 2026-11-16. Owner: Ilya Orange. Branch to use: `stage-windows-port`, based on
`stage-plugin-match`. Related: `docs/WINDOWS_PORT.md`, `.github/workflows/ci.yml`,
`docs/TESTING.md`, `docs/PLUGIN.md`.

## Goal

Windows x64 is a supported build: configure/build exit 0, the whole `SonicatorTests` suite passes,
the VST3 plug-in builds and carries its runtime, and the Windows CI job is no longer
`continue-on-error`. macOS Debug/Release and the universal plug-in + auval must stay green.

This is not a cosmetic port: the current Windows job fails before tests run, so the list in
`docs/WINDOWS_PORT.md` is stale.

## Evidence (run 43782383735, commit 731269e, 2026-11-16)

- **Windows Debug — fails at Build**, exit 1:
  - `core/emu/EblReader.cpp:60`: `error C2440: '<function-style-cast>': cannot convert from
    '_Ty *' to 'juce::CharPointer_UTF16'` (`_Ty = int16_t`).
  - `core/batch/BatchPaths.cpp:3`: `fatal error C1083: Cannot open include file:
    'sys/statvfs.h': No such file or directory`.
- Windows tests therefore never ran; the 23 failures recorded on 2026-11-11 must be
  re-measured after the build is green.
- **macOS Debug — one flaky test**: `TestMatchRenderService.cpp:146`
  `REQUIRE(service.waitUntilFinished(3000))` false. The fake renderer for A waits on its gate for
  up to 3 s and `cancel()` does not interrupt that wait, so B can queue behind it for the whole
  timeout. Fix the harness (release A after cancel) so the suite is deterministic.
- **macOS Release and macOS plugin (universal) — success**, including auval for the AU. This closes
  the macOS plugin gate; record it in `docs/PLUGIN_MATCH_ACCEPTANCE.md` (the Windows session may
  include that small doc update or leave it for the Match follow-up; do not claim the manual
  Cubase/Logic checklist).

## Required sub-skill

Read `superpowers:executing-plans` and `superpowers:test-driven-development` before Task 1. The
Windows work cannot be run locally on the development Mac: GitHub Actions is the only Windows
runner. Use `gh` (authenticated as `orange303-afk`) and treat a CI run as evidence only when the
job log shows the command and its exit code.

## Global constraints

- Never modify the live library (`~/Library/Sonicator`), the live catalog or `exports/`; tests stay
  in `TestDirectory`.
- Do not merge into `main` and do not force-push. Push `stage-windows-port`; open/refresh a PR or
  report the branch and run links.
- Do not delete Windows CI logs or test artifacts.
- A green claim needs: the workflow run URL, the job name, and the actual `ctest` summary line.
- Keep macOS green; every code change must survive the local Debug and Release suites before push.
- Do not mark `docs/WINDOWS_PORT.md` green until a Windows run actually passes.

## Task 1. Make the Windows build compile

1. Reproduce from the logs above (job 133022291439, run 43782383735). Fetch the exact context with
   `gh run view --job 133022291439 --log`.
2. `core/emu/EblReader.cpp:60` — replace the MSVC-invalid cast with a portable UTF-16 conversion
   (`juce::String::fromUTF8`, `juce::CharPointer_UTF16`, or a `juce::String` intermediate) without
   changing what is decoded. Add or extend an EBL unit test if the changed path is reachable.
3. `core/batch/BatchPaths.cpp` — remove the POSIX-only `sys/statvfs.h` dependency. Use
   `juce::File::getBytesFreeOnVolume()` or `GetDiskFreeSpaceExW` behind `#if JUCE_WINDOWS`, keeping
   the existing free-space semantics on macOS. Add a cross-platform unit test for the free-space
   helper if one exists, with a fake value seam rather than a real full disk.
4. Grep the whole tree for other POSIX-only headers (`unistd.h`, `sys/statvfs.h`, `sys/stat.h`,
   `dirent.h`, `pwd.h`) and guard or replace them. `core/match/OfflineMatchRenderer.cpp` already
   guards `unistd.h`.
5. Push and run Windows CI (`gh workflow run ci.yml --ref stage-windows-port`; poll the run).
   Build must reach `SonicatorTests` and `SONICATOR.exe`.

## Task 2. Windows test failures

1. Run the Windows job; collect `LastTestsFailed.log`/`LastTest.log` from the artifact or job log.
2. Classify failures: path separators, pixel tolerance, cleanup ordering, playback. The old list is
   in `docs/WINDOWS_PORT.md` but re-measure.
3. Path separators — pick one design and record it in `docs/WINDOWS_PORT.md`:
   - Catalog paths are stored in native form on the host that scanned them; every repository entry
     point that accepts a path normalises `/` to `juce::File::getSeparatorChar()` on Windows
     (`folderRange`, `upsert`/`upsertLocked`, `findByPath`, ignore/root functions, `removeByPaths`).
   - Tests build expectations with `juce::File` rather than hard-coded `/`.
   - `juce::File` consumers keep working because it accepts both separators.
   Add unit tests that pass a synthetic `C:\lib\...` path on macOS to prove the normaliser and both
   folder-range separators.
4. Pixel/red marker test: fix the assertion to compare the intended colour with a documented
   tolerance, not a platform font metric.
5. `.part` cleanup on cancel: make the test wait for the worker's cleanup (condition-driven), and
   if an open handle blocks deletion on Windows, ensure the exporter closes before checking.
6. Disc-image preview that never starts on Windows: investigate the reader/transport path rather
   than relaxing the timeout; if it is a real Windows defect, fix it and keep the test.
7. Completion contract: Windows `ctest --preset ci-windows` reports 0 failed; local Debug and
   Release full suites still pass.

## Task 3. Windows VST3

1. `plugin/CMakeLists.txt`: request `FORMATS VST3` on Windows and `VST3 AU` on Apple (AU is
   Apple-only). Keep the product/company/codes unchanged.
2. Copy `onnxruntime.dll` next to the built VST3 binary (`Contents/x86_64-win`) in a POST_BUILD
   step under `if(WIN32)`; verify the bundle can resolve it. Do not rely on PATH.
3. Skip the Apple-only smoke target, dylib embedding and codesigning on Windows (the existing
   `if(NOT APPLE) return()` is the place; ensure the VST3 target and DLL copy happen before it).
4. Add a Windows plug-in CI job (Release, `BUILD_TESTING=OFF`, `SONICATOR_BUILD_PLUGIN=ON`) that
   builds `SonicatorPlugin_VST3`, asserts the `.vst3` exists, contains the binary and
   `onnxruntime.dll`, and prints the artefact path. No installer/signing in this stage.
5. Update `docs/PLUGIN.md` and `docs/WINDOWS_PORT.md` with what is actually verified.

## Task 4. Stabilise CI and finish

1. Fix the macOS Debug flake in `tests/TestMatchRenderService.cpp` (release the fake renderer after
   `cancel()`; no fixed sleeps as correctness).
2. Once Windows is green, remove `continue-on-error: true` from the Windows job and make the test
   step fail the workflow. Keep the 90-minute timeout.
3. Update `docs/WINDOWS_PORT.md` (what works, exact counts, run links), `docs/TESTING.md` (Windows
   verified), `AGENTS.md` feature notes, and the external backlog
   `~/Documents/orange303/Sonicator Features.md` without claiming manual DAW/audio-device
   acceptance.
4. Run `graphify update .`; commit and push `stage-windows-port`; report the branch, PR link and
   every CI run URL with its Windows/macOS conclusions.

## Acceptance checklist

- [ ] Windows Debug configure + build exit 0, `SonicatorTests.exe` built.
- [ ] Windows `ctest` 0 failed; counts recorded with the run URL.
- [ ] Windows Release VST3 built, `onnxruntime.dll` present in the bundle.
- [ ] macOS Debug and Release full suites green (no flakes).
- [ ] macOS universal plug-in + auval job green.
- [ ] Windows job is blocking (`continue-on-error` removed) and green.
- [ ] Docs updated with real numbers; no manual audio-device or DAW claim.

## Out of scope

Windows installer, signing, notarization, Windows audio-device/MIDI manual acceptance, AU on
Windows (not a format), and any new feature work.
