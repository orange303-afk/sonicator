# Windows build: current state

SONICATOR builds and runs its test suite on Windows through CI. This file records how far the port
has reached, so the work can continue without re-discovering it. Windows is not yet green: the job is
still marked informational (`continue-on-error`) until every test passes.

Latest Windows run: `CI` workflow, run 43790405360, job "Windows Debug (informational)".

## What works

| Step | State |
|---|---|
| Configure (Visual Studio 2022, x64) | Passed |
| Fetch dependencies (JUCE, libxmp, ONNX Runtime, Catch2) | Passed |
| zlib | Found through the platform build; when absent, `CMakeLists.txt` fetches zlib 1.3.1 and links the static target |
| LAME 3.100 | `configMS.h` enables the SSE quantizer on x64, so `vector/xmm_quantize_sub.c` is compiled too |
| EBL UTF-16 names | `juce::CharPointer_UTF16::CharType` instead of `int16_t`, which MSVC rejects |
| Free-space check | `GetDiskFreeSpaceExW` on Windows, `statvfs` elsewhere, behind one injectable probe |
| `onnxruntime.dll` next to the executables | Copied by a POST_BUILD step under `if(WIN32)` |
| `SonicatorCore`, `SonicatorUI`, `SonicatorTests`, `SONICATOR.exe` | Built |
| Tests | Run: 1274 cases, 1232 passed, 38 failed, 4 skipped (about 3 minutes) |

## Path separators

Catalog paths are stored in the host's native form: the scanner builds them with `juce::File`, so a
Windows catalog holds `C:\Library\Kick.wav`. Every `SampleRepository` entry point that accepts a path
(`folderRange`, `upsert`/`upsertLocked`, `findByPath`, ignore and root functions, `removeByPaths`, the
disc-image functions) passes it through `pathrelocation::normaliseSeparators` with
`juce::File::getSeparatorChar()`. On Windows that rewrites `/` to `\`; on macOS and Linux it is the
identity, because a backslash is a legal filename character there.

Folder scoping is a half-open string range, not an absolutised path: `pathrelocation::folderRange`
turns `C:/lib/Drums` into `["C:\lib\Drums\", "C:\lib\Drums]")`, so the scope matches exactly the rows
the scanner wrote, never a sibling whose name merely starts with the folder name. `juce::File`
consumers keep working because it accepts either separator.

Tests build expectations with `juce::File` or with `getFullPathName()` instead of hard-coded `/`, and
`TestPathRelocation` exercises both separators through the pure helpers on macOS.

## Physical identity

Windows CRT `stat()` leaves `st_ino` at zero, so two equal-sized files written in the same second had
the same `physicalIdentity`. The batch preflight then treated a legitimately different output as an
alias of its source and refused to plan it. `physicalIdentity` now uses the volume serial and the
64-bit file index through `GetFileInformationByHandle`.

## Failing tests

The last measured run had 38 failing cases. The path clusters are fixed on this branch and await a
Windows run; two checks still need evidence from that run:

- `MP3 output is a readable lossy copy and honours the clipping policy`: the CBR half passes, the VBR
  half reports a failed output. The test now prints the pipeline message.
- `Unsupported tracker instrument slots are drawn in red`: the pixel expectation now compares the
  intended colour with a per-channel tolerance and prints the closest distance.

Fixed on this branch and re-measured by the next run:

- Folder scopes and ignores (path separators): database, ignore, stereo pairs, tagging, recent tags,
  auto-tag, folder genre, custom genres, batch preflight.
- `.part` cleanup on cancel: the exporter closes its writer before deleting the part file, which
  Windows requires.
- Drag-cache pruning: a folder's newest file is the age evidence, since JUCE cannot set a directory
  timestamp on Windows.
- Catalog-gold path file: the test parses the JSON instead of matching an escaped backslash.
- Command/Control shortcut equality: on a platform without a Command key JUCE maps it onto Control.
- Cloud-map paint budget: Windows Debug measured 107 ms for the composed frame; its budget is 150 ms.
- Batch sources keep subfolders: the relative directory is split on either separator.

## What is not done

- No Windows installer, signing or notarization.
- No Windows audio device or MIDI acceptance; the tests run without a device.
- The application has never been started on Windows.
- The Windows VST3 bundle is not built by CI yet.

## How to reproduce

The `windows` job in `.github/workflows/ci.yml` runs `cmake --preset ci-windows`, builds, and runs
`ctest --preset ci-windows`. Locally the same commands work in a Developer PowerShell with Visual Studio
2022 and the Desktop C++ workload installed.
