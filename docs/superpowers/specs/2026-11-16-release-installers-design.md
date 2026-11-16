# SONICATOR release installers

## Intent and approved direction

Deliver an installer containing the application and supported DAW plugins on each platform.
The owner has no Developer ID and explicitly selected the unsigned PKG/postinstall approach
already used in `~/Documents/Loop Dicer/Scripts/build-macos-pkg.sh`.

## Scope

- macOS: one universal arm64/x86_64 PKG, minimum macOS 11.0, containing
  `/Applications/SONICATOR.app`, `/Library/Audio/Plug-Ins/VST3/SONICATOR.vst3`, and
  `/Library/Audio/Plug-Ins/Components/SONICATOR.component`.
- Windows: one x64 MSI containing SONICATOR.exe and the VST3 bundle; AU does not exist on Windows.
  Install per-machine under Program Files and Common Files/VST3 respectively.
- Offer independent application/plugin component selection, all selected by default.
- Preserve current product/plugin identities (`com.sonicator.browser`, `com.sonicator.plugin`,
  `Orng`, `Sncr`) and current 0.1.0 version unless the owner requests a version bump.
- Bundle ONNX Runtime and other required non-system runtime dependencies; no build-machine paths.
- Include licence texts/notices and provide corresponding-source/relinking materials appropriate
  to the AGPL/LGPL components before describing a binary as publicly distributable.

## Approaches

1. Developer ID signatures, notarized/stapled PKG: best public distribution experience, but
   unavailable without the owner's Apple Developer credentials; not a prerequisite for this work.
2. Unsigned PKG, ad-hoc-signed payload and scoped quarantine-removal postinstall: selected by
   the owner, matching Loop Dicer. The package itself can still need explicit approval in macOS.
3. Raw bundles/ZIP plus manual Terminal installation: does not meet the requested installer UX.

Windows MSI via pinned WiX is the implementation proposal: supports component selection,
major upgrades, repair and uninstall. No Windows signing certificate is assumed; label unsigned
artifacts honestly and do not promise that SmartScreen will trust them.

## macOS installation behavior

Sign nested runtime libraries before sealing each bundle during packaging. Validate signatures;
never use `codesign --deep` for signing or silently accept failed signing. Postinstall removes
only `com.apple.quarantine`, recursively, from selected installed SONICATOR bundles. It must
respect the installer target volume, reject symlink escapes, and confirm expected bundle IDs.
Executable permissions are set in the staged payload before signing.

Do not scan `/Users/*`, clear arbitrary extended attributes on the installed system, modify
Gatekeeper globally, or re-sign installed code unnecessarily. Removing quarantine does not
provide notarization or guarantee acceptance under every security policy. The installer cannot
approve its own initial launch; document the macOS user-approval procedure without claiming
right-click Open works on every macOS version.

## Safety and exclusions

Never touch catalog/settings/user media on install, upgrade or uninstall. In particular,
`~/Library/Sonicator/exports/` is DAW project media, not cache. Do not replace the owner's
installed app/plugins for testing without permission. Use disposable targets/VMs or isolated CI.
Do not publish a GitHub Release or tag until explicitly authorized. CLAP, Windows ARM64,
automatic updates and general Windows port repair are outside this installer task; discovered
release-blocking defects are reported and fixed only within an agreed scope.

## Evidence gates

Build -> inspect dependencies/architectures/identities -> validate plugins -> sign ad-hoc on
macOS -> package -> inspect installer -> isolated installation/upgrade/uninstall smoke checks.
Verify the macOS app opens with its build-tree runtime unavailable. Validate VST3/AU with
pluginval strictness 10 and AU with auval; run both slices when executable locally or in CI.
Record Windows test failures rather than suppressing them or claiming release readiness.

## Remaining builder decisions

Implement a reusable package-only command per OS plus CI artifact upload, with SHA256 checksums,
logs and exact source commit. Use the existing macOS plugin-universal cache to conserve disk.
Keep local free space above 8 GiB during builds; stop rather than deleting unrelated artifacts.
Actual clean-machine/DAW testing may require the owner's Windows machine and macOS approval.
