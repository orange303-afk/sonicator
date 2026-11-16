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

## Pending evidence (do not infer success)

- Final rebuilt full suite and final packaging tests.
- Fresh-source-commit installer/source archive pair and final checksums.
- New workflow's installed macOS app, AU validation in both slices and reinstall checks.
- Actual Windows Release build, MSI compilation, installed-window/pluginval/lifecycle checks.
- Full Windows suite failures listed in `WINDOWS_PORT.md` remain unresolved by this packaging task.
- Manual macOS/Windows DAW playback/drag/Match acceptance and unsigned-installer approval UX
  on a clean downloaded copy. An unsigned PKG can require manual approval before postinstall.

## Safety

No local installation over the owner's app/plugins, live catalog modification, export cleanup,
global security change, tag or public release was performed. The owner authorized pushing the
working branch and running GitHub Actions to build/test Windows.
