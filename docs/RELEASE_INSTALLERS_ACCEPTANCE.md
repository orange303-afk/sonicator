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

- Run `43939467821`, source `1cce1f1`: Windows compilation cleared the previous three errors,
  then application linking failed because upstream LAME's MSVC x64 config selects
  `init_xrpow_core_sse` but its vector implementation was not in the target. Added that upstream
  translation unit for MSVC only. Real Windows relinking remains to be checked.
- The macOS job in this run **never acquired a runner**, rather than failing a code test.
  Check-run annotation: "The job was not acquired by Runner of type hosted even after multiple
  attempts", with arm64 capacity notices. Packaging CI now targets documented `macos-15-intel`;
  it still builds universal and validates native x86_64. arm64 AU execution is explicitly pending.

- Run **43943920763**, source `2a6745faf4cab0a9a2dabb1a77e995221d566c25`: macOS job
  **SUCCESS** on Intel. The PKG installed, all payloads passed architecture/signature/attribute
  checks, installed application started and quit with build-tree ONNX unavailable, both VST3 and
  AU passed pluginval strictness 10, `auval` reported **AU VALIDATION SUCCEEDED**, and reinstall
  preserved real catalog/settings and synthetic exported-media hashes. Downloaded artifacts to
  `dist/ci-macOS/`; installer and source-archive SHA256 checks both passed locally.
- In that same run Windows **application, VST3 and VST3 manifest built successfully**. The full
  test executable then failed compilation on two POSIX `chmod` calls in a directory-mode test.
  That test now explicitly skips on Windows: POSIX mode bits are not a Windows ACL denial test.
  Windows ACL-denied destination acceptance remains separate, not claimed by that skip.
- Windows compilation parallelism is now bounded per project (2 compiler processes by default
  for release; 4 on CI only if at least 12 GiB RAM), with serialized MSBuild projects. Diagnostic
  full-suite compilation runs after installer lifecycle checks to expose packaging failures first.
- Windows installed-app smoke fixture explicitly opts out of contribution sharing in a fresh
  disposable settings profile. This avoids mistaking expected first-launch modal consent for
  failure to close the application; it never overwrites an existing profile.

## Pending evidence (do not infer success)

Run `43953066975` (`c08e068`) built the Windows application/VST3 and both the real MSI and higher-
version upgrade fixture successfully. The first install failed at `LaunchConditions` (1603):
MSI's compatibility properties reported `VersionNT64=603`, `WindowsBuild=9600`. Those values are
documented by Microsoft as deliberately compatibility-limited. The gate now uses a secure
64-bit registry search for `CurrentBuildNumber`, as recommended in WiX's launch-condition docs,
and allows `Installed` so maintenance/uninstall are not blocked. Actual installation rerun remains
required; successful MSI creation is not installation success. macOS in this run passed again.

Run `43957871104` (`674237d`) proved the launch-condition fix: the MSI installed, both features
were present with their runtimes, the installed application opened a window and exited 0, and the
test fixture profile produced catalog/settings/export files. The run then failed on our own check,
not on pluginval: the log ended with `SUCCESS` and no `*** FAILED`, but the script compared
`$LASTEXITCODE`, which PowerShell never sets for processes started with `Start-Process`. The
lifecycle script now reads the raw process exit code and stderr from `Start-Process -PassThru`.

Run `43961940936` (`afbe366`): **macOS SUCCESS** and the whole Windows lifecycle **passed** —
install, installed window start/close, pluginval strictness 10 for the installed and build-tree
VST3 (both exit 0), repair, 0.1.1 major upgrade, per-feature installs, uninstall, and unchanged
catalog/settings/export hashes. That run then failed only on the new ONNX control: the Catch2
filter was the literal test name, whose comma Catch2 splits into two specs that match nothing, so
the negative control was vacuous. The control now uses a wildcard filter and requires the negative
run to be non-zero without `All tests passed` and the positive run to be zero with it.

- Full suite after Windows portability corrections and final packaging tests.
- Fresh-source-commit installer/source archive pair and final checksums.
- New arm64 AU execution (Intel CI does not prove it); Intel installed-app/AU/reinstall gates passed.
- Successful Windows Release build, MSI compilation, installed-window/pluginval/lifecycle checks.
- Full Windows suite failures listed in `WINDOWS_PORT.md` remain unresolved by this packaging task.
- Manual macOS/Windows DAW playback/drag/Match acceptance and unsigned-installer approval UX
  on a clean downloaded copy. An unsigned PKG can require manual approval before postinstall.

## Safety

No local installation over the owner's app/plugins, live catalog modification, export cleanup,
global security change, tag or public release was performed. The owner authorized pushing the
working branch and running GitHub Actions to build/test Windows.
