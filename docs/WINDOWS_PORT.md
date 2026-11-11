# Windows build: current state

SONICATOR has never been built or run on Windows. Everything recorded here comes from a CI job that is
being established; a green job is not claimed until it exists.

## What is known before the first build

- `find_package(ZLIB REQUIRED)` in the root `CMakeLists.txt`: Windows has no system zlib. The CI job is
  expected to supply it through vcpkg (`x64-windows` or `x64-windows-static`); the choice decides whether
  `zlib1.dll` must sit next to the executables.
- ONNX Runtime is fetched as a prebuilt `win-x64` archive and imported as `onnxruntime.dll`. Nothing
  copies that DLL next to `SONICATOR.exe` or `SonicatorTests.exe`; on Windows `BUILD_RPATH` has no effect,
  so a POST_BUILD copy step under `if(WIN32)` is required before the tests can start.
- libxmp, JUCE, SQLite and Catch2 are CMake-native and expected to configure.
- The source tree has not been compiled with MSVC. Unknown: deprecation-as-error cases, `#pragma`
  behaviour, `wchar_t`/`juce::String` conversions and any POSIX-only call outside the existing
  `_WIN32` guards.

## Status

| Step | State |
|---|---|
| Configure | Not run |
| `SonicatorCore` | Not run |
| `SonicatorUI` | Not run |
| Application | Not run |
| Tests | Not run |

Update this file with the CI run link, the first error of each kind (file, line, verbatim text, count)
and an estimate of the work: mechanical, or requiring a decision.
