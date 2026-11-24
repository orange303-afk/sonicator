# Windows build: current state

SONICATOR is built and tested on Windows. This file records how far the port has reached, so the
work can continue without re-discovering it.

Local run that this state describes: Windows 11 x64, Visual Studio 2022 (MSVC 14.43), CMake preset
`ci-windows`, Debug, 2026-11-24.

## What works

| Step | State |
|---|---|
| Configure (Visual Studio 2022, x64) | Passed |
| Fetch dependencies (JUCE, libxmp, ONNX Runtime, Catch2) | Passed |
| zlib | Found through the platform build; when absent, `CMakeLists.txt` fetches zlib 1.3.1 and links the static target |
| `onnxruntime.dll` next to the executables | Copied by a POST_BUILD step under `if(WIN32)` |
| `SonicatorCore`, `SonicatorUI`, `SonicatorTests`, `SONICATOR.exe` | Built |
| Tests | **1320 cases: 1311 passed, 9 skipped, 0 failed** (268 525 assertions), also green in random order |

The suite had 22 failures in 2026-11-11 and 40 by 2026-11-21; every one was either a real Windows
defect or a test that assumed POSIX path text. The list below is what changed.

## What the port changed in the product

### Path separators are a catalog invariant

Every catalog comparison is a string comparison, so a row must be stored with the platform
separator. `SampleRepository` now folds a caller path to the native separator on the way in
(`upsert`, `findByPath`, roots, ignore entries, disc images, symlink flags, removal), and a folder
scope uses the path as given instead of round-tripping through `juce::File`, which on Windows turned
`/lib/Drums` into `C:\lib\Drums`.

A catalog written on another system, or by an older build, is made uniform by one idempotent pass at
open (`normaliseCatalogPaths`, Windows only): `samples.path`, `container_path`, `roots.path`,
`ignored_paths.path`, `disc_index.image_path` and `smart_inputs.path` lose a foreign `/`. Archive
paths (`disc_path`, `disc_entry`, `disc_parent`, `disc_partner`) are not filesystem paths and stay
untouched. On POSIX nothing is rewritten, because a backslash is a legal file-name character there.

### Folder tree and favourites compare both conventions

`FolderIndex` builds its roots with `pathrelocation::toNative` and matches with
`pathrelocation::isUnder`, so a favourite saved on another system still finds the catalogued rows.
`FolderTree::selectPath` compares native forms; a `File` comparison would not, because JUCE's `File`
equality does not normalise separators.

### Batch preflight identity

`physicalIdentity` used `stat()`, whose `st_ino` is `0` on Windows: two different files with the same
size and second shared one identity, and the preflight refused a legitimate output as "the same
physical file as a source". Windows now asks `GetFileInformationByHandle` for the volume serial, file
index, size and write time.

### Cancelled exports leave no `.part` file

Windows refuses to delete a file that still has an open handle, so the exporter closes its writer
and stream before the failure helper removes the partial file. The order did not matter on POSIX.

### Pixel tests read after the Graphics ends

An image on Windows is backed by a Direct2D bitmap whose frame is submitted when the graphics context
is destroyed; a live `Graphics` still reads the empty main-memory copy. The scrollbar-lane and
tracker-marker tests now scope the `Graphics` before reading pixels.

## Skipped tests and why

Nine tests skip on Windows, each with the reason in its message:

- symbolic links in the file-safety tests need Developer Mode or an elevated process;
- the duplicate-cleanup link replacement reports "Cannot create a symbolic link here" for the same
  reason, and the test skips on that state;
- write-denied destination uses POSIX mode bits, which Windows ACLs do not implement;
- the drag-cache pruning test cannot backdate a directory (`setLastModificationTime` returns false on
  Windows directories), so the pruning decision is not exercised there.

A CI Windows image without Developer Mode will skip the link tests too; that is recorded, not hidden.

## What is not done

- The Windows job in `.github/workflows/ci.yml` is still informational (`continue-on-error`). The
  local suite is green; promoting the job needs one green CI run first (see `docs/TESTING.md`).
- Windows MSI packaging is implemented; the installer matrix is in
  [RELEASE_INSTALLERS_ACCEPTANCE.md](RELEASE_INSTALLERS_ACCEPTANCE.md). No signing certificate is
  assumed. Apple notarization does not apply to Windows.
- No Windows audio device or MIDI acceptance; the tests run without a device.
- The application has not been started by hand on Windows in this stage: the launch, the file
  dialogs, drag to Explorer and the installer are owner checks.

## How to reproduce

The `windows` job in `.github/workflows/ci.yml` runs `cmake --preset ci-windows`, builds, and runs
`ctest --preset ci-windows`. Locally the same commands work in a Developer PowerShell with Visual
Studio 2022 and the Desktop C++ workload installed.
