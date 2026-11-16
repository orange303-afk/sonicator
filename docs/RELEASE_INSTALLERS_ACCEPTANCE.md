# Release installers — acceptance evidence

Date: 2026-11-16. Work branch: `stage-release-installers`.

## Local evidence

- Before changes: existing Release suite passed, **1279 cases / 272517 assertions**.
- `cmake --preset release-macos-installer` and its build preset: exit 0; universal application,
  VST3 and AU built in the existing plugin-universal cache.
- `python3 -m unittest discover -s packaging/tests -v`: **12 tests passed** before remote CI.
  Covers real embedded-library execution without its source copy, universal verifier, missing
  inputs, quarantine removal/attribute preservation, alternate-volume paths, wrong identity,
  symlinked parents/children, unselected payload preservation and stable MSI file components.
- `sh packaging/macos/build-pkg.sh ... dist`: exit 0. Created unsigned
  `dist/SONICATOR-0.1.0-macOS-universal.pkg` and SHA256 file.
- `pkgutil --expand-full`: exit 0. All three payloads and their component-scoped scripts are
  present. PackageInfo reports `relocatable="false"`; product offers three independent choices.
- pluginval **1.0.4**, strictness **10**, `--skip-gui-tests`, against the VST3 extracted from the
  generated PKG: **SUCCESS**, exit 0. No owner-installed plugin was replaced.
- Fresh local package from source commit `00682cd813bcef558502e1ff0ac0a579ff34b2a2` also passed
  pluginval strictness 10 after correcting runtime search paths. A matching source/relink archive
  and checksums are in `dist/`.
- `sh packaging/tests/test-macos-pkg.sh dist/SONICATOR-0.1.0-macOS-universal.pkg`: exit 0.
  Expanded the actual product, checked all three choices/non-relocation, ran its own packaged
  postinstall scripts twice on isolated payload roots with seeded quarantine, then verified
  quarantine absence, unchanged signatures and bundled licence materials.
- Local `ctest --preset release --output-on-failure` after runtime loader changes: **100% passed**,
  monolithic suite, 37.93 s. Further Windows portability changes below are being rebuilt separately.
- Real PowerShell CRT-discovery fixture passed on macOS with PowerShell 7.4.6, after a failing
  pre-implementation run. All Windows packaging scripts passed the PowerShell parser.

## Remote CI observations

- Owner-authorized workflow run `43935404997`, source `00682cd`: Windows packaging Python tests
  and PowerShell CRT fixture passed. Windows product compilation failed on three concrete issues:
  `EblReader.cpp` used `int16_t*` where Windows JUCE requires its own UTF-16 character type;
  `BatchPaths.cpp` included POSIX-only statvfs/unistd; Windows `min` macro broke the new ONNX file.
  These are being corrected; no MSI build/install success is claimed for this run.

Local portability-fix verification: rebuilt SonicatorTests successfully, then
`ctest --preset release --output-on-failure` passed **100%**, 36.57 s, including the new EBL
UTF-16 surrogate-pair and Unicode/free-space preflight cases. Packaging Python tests: **12/12**.
The next Windows CI run, not this macOS result, must confirm MSVC compilation.

## Pending evidence (do not infer success)

- Full suite after Windows portability corrections and final packaging tests.
- Fresh-source-commit installer/source archive pair and final checksums.
- New workflow's installed macOS app, AU validation in both slices and reinstall checks.
- Successful Windows Release build, MSI compilation, installed-window/pluginval/lifecycle checks.
- Full Windows suite failures listed in `WINDOWS_PORT.md` remain unresolved by this packaging task.
- Manual macOS/Windows DAW playback/drag/Match acceptance and unsigned-installer approval UX
  on a clean downloaded copy. An unsigned PKG can require manual approval before postinstall.

## Safety

No local installation over the owner's app/plugins, live catalog modification, export cleanup,
global security change, tag or public release was performed. The owner authorized pushing the
working branch and running GitHub Actions to build/test Windows.
