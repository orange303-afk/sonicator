# Windows build: current state

SONICATOR is now built on Windows by CI. This file records how far the port has reached, so the work
can continue without re-discovering it. A green Windows job is not claimed: tests fail, and the job is
marked informational (`continue-on-error`) on purpose.

First run with a completed build: `CI` workflow, 2026-11-11.

## What works

| Step | State |
|---|---|
| Configure (Visual Studio 2022, x64) | Passed |
| Fetch dependencies (JUCE, libxmp, ONNX Runtime, Catch2) | Passed |
| zlib | Found through the platform build; when absent, `CMakeLists.txt` fetches zlib 1.3.1 and links the static target |
| `onnxruntime.dll` next to the executables | Copied by a POST_BUILD step under `if(WIN32)`; the tests start and load the runtime |
| `SonicatorCore`, `SonicatorUI`, `SonicatorTests`, `SONICATOR.exe` | Built |
| Tests | Run: 656 cases, 629 passed, 23 failed, 4 skipped (about 2 minutes) |

So the source tree compiles with MSVC without edits. The first build needed only build-system changes:
the zlib fallback and the runtime DLL copy.

## Failing tests

All 23 failures are assertions, not crashes, and they cluster around path separators and two playback
and drawing checks. The list below is the exact set from the latest run; each failed on the first
assertion of its case.

### Folder scoping, ignore lists and stereo pairs (path separators)

- Ignored rows leave queries, counts, folder scopes and duplicate groups
- Large ignore lists keep queries working
- visibleIds keeps only rows that are not ignored
- Smart verification skips ignored completed results
- A valid mono L/R pair is one library row with its partner path
- Folder queries include every descendant but not sibling prefixes
- Whole-catalog search is case-insensitive, token-based and escapes wildcards
- Added folder tree includes deep descendants and responds to arrow keys
- Nested added roots share the same descendant hierarchy
- Folder tree reports a right-click with its path and All samples state
- Favorite tree keeps a folder and its catalogued descendants expandable
- Folder tree selects a nested restored path
- Folder tree hides ignored folders unless Show ignored is on
- Folder id scope is recursive and separator-aware
- Folder tag job is recursive, idempotent and records one recent tag
- Clearing tags removes links recursively and is idempotent
- Folder reset removes every tag link and preserves stars, overrides and history
- Selected tag jobs skip ignored rows
- AutoTag folder preparation is recursive and excludes siblings
- Relocating a root moves disc containers and index state

Example failures:

```
tests\TestDatabase.cpp(14): FAILED:
  REQUIRE( repo.countInFolder("/lib/Drums") == 2 )
tests\TestUI.cpp(94): FAILED:
  REQUIRE( selection == rootPath+"/deep" )
tests\TestIgnore.cpp(84): FAILED:
  REQUIRE( repo.query(f).total == 1 )
```

These tests build their expectations with `/`. On Windows the repository writes and compares
`File::getSeparatorChar()`. The suspects, in order:

1. test data written with `/` and compared against `\` paths;
2. folder scopes that treat only one separator as a boundary (`isPathIgnored`, `countUnderRoot`,
   `FolderTree` subtree matching);
3. `selectPath` in the folder tree cannot match a stored path that uses the other separator.

Category 1 is mechanical. Category 2 and 3 may be real defects on Windows and need a decision: either the
repository normalises separators inside all queries, or every caller is required to pass native paths.

### Two single failures

- `Unsupported tracker instrument slots are drawn in red`: the pixel check for the red marker differs on
  Windows, probably font metrics or colour rounding. Could be the test's tolerance, not the UI.
- `Long exports run in bounded chunks and cancel cleanly`: asserts that no `.part` file survives a
  cancellation. Either cleanup ordering differs on Windows (an open handle blocks deletion) or the check
  runs before the worker's cleanup.
- `Changing image sample stops old playback before the new decode completes`: the test waits up to two
  seconds for the transport to start (the wait is condition-driven, not a fixed sleep), and on Windows
  the button never shows "Pause". This is not slowness: disc-image preview does not start playing on
  Windows in this environment, which is a real difference to investigate, not a test tolerance.

## What is not done

- No Windows installer, signing or notarization.
- No Windows audio device or MIDI acceptance; the tests run without a device.
- The application has never been started on Windows.
- `docs/TESTING.md` records Windows as unverified; that stays true until the 22 failures are resolved.

## How to reproduce

The `windows` job in `.github/workflows/ci.yml` runs `cmake --preset ci-windows`, builds, and runs
`ctest --preset ci-windows`. Locally the same commands work in a Developer PowerShell with Visual Studio
2022 and the Desktop C++ workload installed.
