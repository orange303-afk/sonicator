# SONICATOR Release Installers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build macOS universal PKG and Windows x64 MSI installers for the application and plugins, with scoped macOS quarantine removal.

**Architecture:** Extend current CMake artifact handling so all payloads are self-contained. Package three independently selectable macOS components and two Windows features with stable upgrade identities. Unsigned distribution is explicit; packaging never disables system-wide protections.

**Tech Stack:** CMake/JUCE, POSIX shell, pkgbuild/productbuild, PowerShell, pinned WiX, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-16-release-installers-design.md`

## Global Constraints

- macOS: universal arm64/x86_64, minimum macOS 11.0; Windows: x64, no AU.
- Version remains 0.1.0; bundle IDs and plugin codes remain unchanged.
- No Developer ID or Windows signing certificate assumed; no notarization claim.
- Preserve catalog/settings/exported audio, including on upgrade/uninstall.
- No global Gatekeeper changes, `/Users/*` scans or changes to unrelated installed code.
- No publication/tagging or replacement of the owner's installed components without permission.
- Reuse the macOS plugin-universal build directory; stop below 8 GiB free space.

## Review Focus

1. Non-default target volume or symlinked installation path: postinstall cannot escape its target.
2. Missing/unselected component: cannot silently ship an incomplete default installer or modify an old unselected bundle.
3. Nested runtime dependencies: plugins/app must load without build-tree DLL/dylib search paths.
4. Existing installation: upgrades replace product files without touching the shared catalog or media.
5. Script/signature failure: installer/build logs identify it, never silently claim success.

---

### Task 1: Self-contained release payloads

**Files:** modify `CMakeLists.txt`, `plugin/CMakeLists.txt`, `CMakePresets.json`;
create `cmake/EmbedMacRuntime.sh`, `packaging/tests/test-release-contracts.py`.

**Interfaces:** presets `release-macos-installer` (existing plugin-universal binaryDir) and
`release-windows-installer` (Release x64, plugin ON); build targets `Sonicator`,
`SonicatorPlugin_VST3`, plus `SonicatorPlugin_AU` only on Apple.

- [ ] Add failing contract tests for platform formats, runtime embedding/copy steps and release presets. Use Python stdlib unittest for packaging developer tests only, not product runtime.
- [ ] Run `python3 -m unittest discover -s packaging/tests -v`; confirm failures name missing release support.
- [ ] Embed ONNX inside macOS app with relative loader path and inside-out ad-hoc signing. Generalize existing plugin embedding only where needed.
- [ ] On Windows request only VST3, embed the required runtime beside the VST3 module as well as the exe, and ensure the module manifest can load at build time. Audit transitive PE imports/MSVC runtime strategy before freezing payloads.
- [ ] Add release presets with test/training/smoke artifacts excluded from installer payloads, without breaking current test builds.
- [ ] Run contracts; build universal app/VST3/AU. Check lipo, otool and codesign for every shipped binary. Verify loading from an isolated copy without access to the build-tree runtime.
- [ ] Commit only task files after reviewing diff.

### Task 2: macOS selectable PKG and scoped postinstall

**Files:** create `packaging/macos/build-pkg.sh`, `packaging/macos/postinstall`,
`packaging/macos/Distribution.xml.in`, `packaging/macos/README.txt`,
`packaging/tests/test-macos-pkg.sh`; extend contract tests.

**Interfaces:** `build-pkg.sh BUILD_DIR OUTPUT_DIR` consumes CMake Release artifacts and
project version; produces `SONICATOR-0.1.0-macOS-universal.pkg` and SHA256 checksum.
Per-component script receives only its installed bundle identity/path, with Installer `$3`
as target volume; no eval/untrusted shell substitution.

- [ ] Add failing tests for missing artifacts, absent architecture/runtime, spaces in paths, unselected bundles, symlink escapes, alternate target volume and postinstall error logging.
- [ ] Run tests and capture expected failure before adding packaging code.
- [ ] Stage exact three payload bundles in private temporary directories outside iCloud; set executable permissions before sealing and verify nested signatures. Never mutate source artifacts while staging.
- [ ] Generate app/VST3/AU component packages and a productbuild distribution with all components selected by default and stable package IDs. Disable bundle relocation and avoid copying smoke/development files.
- [ ] Add per-component postinstall: target only its own selected payload, validate target root and bundle ID, reject symlinks, remove quarantine only, report genuine failures. Missing quarantine attributes are not errors. Do not re-sign after installation.
- [ ] Include installation warning for the unsigned PKG and accurate user-approval instructions. No automatic Gatekeeper changes.
- [ ] Run shell syntax/contract/fixture tests; inspect expanded PKG scripts, choices, payloads and version. Run installation on an explicitly approved disposable target; check permissions, quarantine, signatures, dependency closure and reinstall behavior.
- [ ] Commit task files after reviewing diff.

### Task 3: Windows MSI with app/VST3 features

**Files:** create `packaging/windows/build-msi.ps1`, `packaging/windows/Sonicator.wxs`,
`packaging/windows/test-install.ps1`; extend contract tests.

**Interfaces:** `build-msi.ps1 -BuildDir <path> -OutputDir <path>` consumes x64 Release
artifacts and produces `SONICATOR-0.1.0-Windows-x64.msi` with SHA256 checksum.

- [ ] Add failing contract tests for app/VST3 feature selection, stable UpgradeCode, x64 per-machine paths, missing DLL/module rejection and preservation of user data.
- [ ] Run contracts and confirm the missing installer definitions fail.
- [ ] Implement pinned WiX tooling, deterministic product version/upgrade handling and component identities; app under Program Files/SONICATOR, plugin under Common Files/VST3/SONICATOR.vst3. Add Start-menu shortcut and clean uninstall of product files only.
- [ ] Include runtime closure and licence materials; leave unsigned explicitly rather than claiming SmartScreen trust. Exclude AU and dev artifacts.
- [ ] Run build and silent MSI install on a disposable Windows runner. Test default/app-only/plugin-only selection, repair, reinstall and uninstall; exercise major upgrade with a temporary higher test version. Assert sentinel catalog/settings/exports files remain byte-identical.
- [ ] Run VST3 module discovery/pluginval strictness 10 on the installed runtime layout; save logs.
- [ ] Commit task files after reviewing diff.

### Task 4: Release artifact workflow and acceptance record

**Files:** create `.github/workflows/release-installers.yml`, `docs/RELEASE_INSTALLERS.md`,
`docs/RELEASE_INSTALLERS_ACCEPTANCE.md`; update `docs/PLUGIN.md`, `docs/WINDOWS_PORT.md`,
`THIRD_PARTY_NOTICES.md` as required by actual results; add licence-material collection under `packaging/`.

**Interfaces:** manual workflow builds OS jobs, validates, packages and uploads installers/logs/checksums as workflow artifacts only. No GitHub Release creation/tagging.

- [ ] Add contract checks that release jobs enable plugins/Release, fail required validation gates and do not publish automatically.
- [ ] Implement separate macOS/Windows jobs with pinned tools, dependency caching, pluginval/AU checks and isolated installer smoke checks where supported. Preserve informational Windows test status honestly; do not turn known failing tests green by ignoring errors.
- [ ] Collect actual licence texts/notices from pinned dependencies, including nested JUCE components, and record corresponding source/relinking delivery requirements. Block public-release claims if materials are incomplete.
- [ ] Run packaging contract tests and existing relevant app/plugin tests. Run the new workflow only after its code is available on an authorized remote branch; inspect actual results/download produced artifacts rather than infer CI success.
- [ ] Record each performed check, exact commit/tool versions, artifact hashes and remaining manual checks. Missing Windows execution or clean-machine/DAW tests stay explicitly open.
- [ ] Run `graphify update .`, inspect diff and perform independent final review if authorized. Commit only intended changes; ask before publication or installation over the owner's files.

## Execution status

- [x] Existing CMake, CI, runtime/signing helpers, Loop Dicer PKG and Windows port notes inspected.
- [x] Design direction approved by the owner's request to proceed.
- [ ] Implementation plan reviewed and execution method selected.
- [ ] Tasks 1–4 implemented and verified; acceptance evidence recorded.
