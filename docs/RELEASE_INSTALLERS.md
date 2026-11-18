# Release installers (unsigned)

SONICATOR currently ships without Developer ID/notarization or Windows Authenticode.
An installer build is not the same as a verified public release. Actual checks and
remaining limits are in [RELEASE_INSTALLERS_ACCEPTANCE.md](RELEASE_INSTALLERS_ACCEPTANCE.md).

## Deliverables

- `SONICATOR-0.1.1-macOS-universal.pkg`: independently selectable application, VST3 and AU;
  arm64 + x86_64, macOS 11 or newer.
- `SONICATOR-0.1.1-macOS-x86_64-10.15.pkg`: the same three components for the Intel Macs that
  cannot install the universal package; x86_64 only, macOS 10.15 or newer, no ONNX Runtime.
- `SONICATOR-0.1.1-Windows-x64.msi`: independently selectable application and VST3;
  64-bit Windows 10 or newer. AU is macOS-only.
- `sonicator-manual.pdf`: the user manual.

**A GitHub release publishes the installers and the manual, and nothing else.** Users download
exactly one file for their system; checksums, per-build source archives and `source-commit.txt` stay
in the workflow artifacts, and the AGPL source obligation is met by the public repository at the
release tag, named in the release notes with the exact commit.

Platform source archives, when someone needs one, contain their own platform's ONNX runtime package
(and Windows zlib sources where fetched): the same filename with platform-specific content, so keep
each next to the build it came from.

## macOS

```bash
cmake --preset release-macos-installer
cmake --build --preset release-macos-installer
sh packaging/macos/build-pkg.sh "$HOME/Library/Caches/sonicator-build/plugin-universal" "$PWD/dist"
python3 packaging/make-source-bundle.py "$HOME/Library/Caches/sonicator-build/plugin-universal" "$PWD/dist"
```

Commit the matching sources before creating the source archive: the archive is taken from
`git archive HEAD`, so uncommitted working-tree edits are never shipped as “matching source” and
a Windows CRLF-normalized checkout cannot block packaging. Builders refuse to overwrite an
existing artifact, and macOS staging stops if less than 8 GiB is available. Build/staging are kept
outside iCloud; final installers go in ignored `dist/`. Python 3.8+ is needed by source-archive
creation (developer tooling only). Product operation needs no Python.

Locations:

- `/Applications/SONICATOR.app`
- `/Library/Audio/Plug-Ins/VST3/SONICATOR.vst3`
- `/Library/Audio/Plug-Ins/Components/SONICATOR.component`

The app and plugins of a build with ONNX enabled embed the runtime under `Contents/Frameworks`,
with relative load commands. A build without it carries no nested dylib at all. That runtime is
built for 13.3, so a universal package that embeds it cannot run below 13.3 whatever minimum it
declares; the x86_64-10.15 package omits it and is what older systems install.
The package stages clean copies, includes licence texts, signs nested code before the bundle,
and checks both architectures, non-system dependency paths, bundle signatures and version.
These are **ad-hoc** signatures, not Apple Developer ID signatures.

Each selected component's postinstall removes only `com.apple.quarantine` from its own bundle.
It uses Installer's target volume, checks the bundle ID and rejects symlinked paths/contents.
It does not scan other users, clear unrelated attributes, re-sign installed code or change
global Gatekeeper policy. A missing selected payload or failed fixup fails visibly.

The unsigned PKG itself may be blocked before its scripts can execute. After attempting to open
it, the user may need **System Settings → Privacy & Security → Open Anyway**, if offered by that
macOS version/policy. Do not promise right-click Open or automatic approval on every Mac.

Quit the app and DAWs before upgrading. Open the app first to create/migrate the shared library;
the plugin deliberately never migrates it. Restart/rescan hosts after installation. Old per-user
copies of the plugin are not deleted; if a host sees duplicate versions, the owner must choose
which old installation to remove.

### Old Intel Macs (macOS 10.15)

`SONICATOR-<version>-macOS-x86_64-10.15.pkg` is an additional package for the same version. It
does not replace or rebuild the universal one.

```bash
cmake -S . -B build-legacy -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DBUILD_TESTING=OFF \
  -DSONICATOR_ENABLE_ONNX=OFF -DSONICATOR_BUILD_PLUGIN=ON
cmake --build build-legacy --target Sonicator SonicatorPlugin_VST3 SonicatorPlugin_AU
sh packaging/macos/build-pkg.sh build-legacy dist legacy-x86_64
sh packaging/tests/test-macos-pkg.sh "dist/SONICATOR-<version>-macOS-x86_64-10.15.pkg" legacy-x86_64
python3 packaging/make-source-bundle.py build-legacy dist -x86_64-10.15
```

`CMAKE_OSX_DEPLOYMENT_TARGET` is set before `project()` for every build: after it CMake ignores
the value and the binaries inherit the SDK's own minimum (15.0 on a current runner). The x86_64
slice then reports 10.15, while the arm64 slice is promoted to 11.0 by the toolchain, so the
universal package covers both.

ONNX Runtime is left out because its macOS dylib requires 13.3 and the application never loads a
model; the neural classifier falls back to the acoustic rules, exactly as it behaves in every
current build. The test suite keeps the classifier on: `-DSONICATOR_ENABLE_ONNX=OFF` together
with `BUILD_TESTING=ON` stops the configure with an explicit message.

The package is gated on `lipo` (x86_64 only, no arm64 slice), `minos` ≤ 10.15 for the app, VST3
and AU, relocatable dependencies and an ad-hoc signature. Booting it on a real 10.15 system is not
reproducible on Apple Silicon and stays a manual check by the owner or the reporting user.

## Windows

From Developer PowerShell with Visual Studio 2022 C++ x64 tools, CMake, Python and .NET 8:

```powershell
cmake --preset release-windows-installer
cmake --build --preset release-windows-installer
./packaging/windows/build-msi.ps1 -BuildDir build -OutputDir dist
python packaging/make-source-bundle.py build dist
```

The packager uses WiX **5.0.2** and its matching UI extension. The MSI is per-machine and supports
feature selection, repair, same-version reinstall, major upgrade, downgrade refusal and uninstall.
The application goes under Program Files/SONICATOR; VST3 under Common Files/VST3/SONICATOR.vst3.
ONNX and redistributable MSVC CRT DLLs are shipped app-locally in both loading directories.
There is no global VC-redist installation custom action. Unsigned Windows installers/binaries
may prompt SmartScreen; no trust/reputation bypass is attempted.

The Start-menu shortcut is deliberately not advertised: an advertised shortcut takes its icon from
an MSI `Icon` row, and Windows shows a blank shortcut when that row is missing. A plain shortcut
uses the icon embedded in `SONICATOR.exe`, and a taskbar pin keeps that executable path across major
upgrades. The MSI still embeds the same `icon.ico` (generated by juceaide from the branding PNG) for
the Apps & features entry. The window also sets its icon on the peer at startup, so the title bar
and taskbar do not depend on Explorer's icon cache. `test-install.ps1` checks the shortcut target
and the executable icon.

The release preset serializes MSBuild projects and sets `SONICATOR_MSVC_COMPILER_JOBS=2` for
parallel translation-unit compilation. CI selects 4 workers only with at least 12 GiB RAM.
Other presets retain their original compiler-parallelism behavior.

Never run `packaging/windows/test-install.ps1` on a working user's installation. It is guarded
for a disposable GitHub runner, where it exercises installed app window creation, pluginval,
repair, a higher-version upgrade fixture, feature selections and uninstall, while checking
catalog/settings/export hashes. The temporary 0.1.2 MSI is a **test fixture**, not a release.
The disposable startup profile explicitly opts out of contribution sharing, avoiding the
first-launch consent modal during the shutdown test. It refuses to overwrite an existing
profile; interacting with the first-launch dialog is separate manual acceptance.

## Linux

```bash
cmake --preset ci-linux
cmake --build --preset ci-linux
xvfb-run --auto-servernum ctest --preset ci-linux
sh packaging/linux/build-packages.sh build dist
python3 packaging/make-source-bundle.py build dist
```

Deliverables: `SONICATOR-<version>-Linux-x86_64.AppImage` and
`SONICATOR-<version>-Linux-amd64.deb`, published alone like the other platforms. The script also
writes `SONICATOR-<version>-Linux.sha256` and CI keeps it with the build artifacts.

Version 0.1.1 publishes these next to the stable macOS and Windows assets with an explicit
in-development warning in the release notes: the package contents, the suite and the headless start
are verified, the desktop behaviour is not. Keep that wording until `docs/LINUX_ACCEPTANCE.md` is
filled in.

**The container is the release baseline.** An AppImage carries no glibc, so the glibc of the machine
that builds it becomes the floor for every user. Release artifacts are therefore built inside
`ubuntu:22.04` (glibc 2.35), which covers Ubuntu 22.04+, Debian 12+, Mint 21+ and Fedora 36+.
`build-packages.sh` reads every `GLIBC_` symbol version from the shipped binaries and refuses to
produce a package above 2.35, so a build on a newer distribution fails loudly instead of shipping.

Layout of both artifacts: `usr/bin/SONICATOR`, `usr/lib/sonicator/libonnxruntime.so` (resolved
through `$ORIGIN/../lib/sonicator`), `usr/share/applications/sonicator.desktop`, hicolor icons at
16–512 px and licences under `usr/share/doc/sonicator/Licenses`. The AppImage adds an `AppRun` that
exports the runtime directory. The `.deb` declares `libc6 (>= 2.35)`, `libasound2`, the X11 libraries JUCE links (`libx11-6`,
`libxext6`, `libxrandr2`, `libxinerama1`, `libxcursor1`, `libxrender1`, `libxi6`), `libfreetype6`,
`libfontconfig1`, `libgl1` and `libjack-jackd2-0 | pipewire-jack`, so either JACK implementation
satisfies it. The release job reads the produced package back and fails when a JUCE library is
missing from that list.

Install with `sudo apt install ./SONICATOR-<version>-Linux-amd64.deb`, or run the AppImage from any
directory after `chmod +x`. Without FUSE the AppImage starts with `--appimage-extract-and-run`.

There is no signature, no distribution repository and no `libjack` bundled: the release notes say so.
Upgrading is a new `.deb` install or a replaced AppImage; nothing touches the library, settings or
exported audio. A VST3 plug-in for Linux is not part of this release.

## Validation and CI

```bash
python3 -m unittest discover -s packaging/tests -v
```

`.github/workflows/release-installers.yml` builds both platforms and uploads workflow artifacts;
it does not create tags or GitHub Releases. It is manually dispatchable, and also runs on the
installer development branch. Required checks fail the job, rather than uploading an installer
as successfully validated. Logs upload even when validation fails.

The macOS runner installs the PKG into its disposable system, hides the build-tree ONNX library,
opens the installed application, validates plugins with pluginval 1.0.4 strictness 10, validates
the native AU slice with auval, and reinstalls while checking user-data hashes. It currently uses
`macos-15-intel`: arm64 runner allocation repeatedly failed due to GitHub capacity constraints.
The binary is still universal, but arm64 AU execution remains an explicit open gate. If moved
back to arm64, the workflow also checks x86_64 through Rosetta. Both pluginval GUI tests and
manual DAW/audio acceptance remain separate.

The existing full Windows test suite has historical failures documented in WINDOWS_PORT.md.
MSI smoke-test success must not be presented as proof those failures have been fixed.
No installer writes/removes the catalog, preferences, user sample libraries, or
`~/Library/Sonicator/exports/` (including drag and Match audio referenced by DAW projects).

## Publication gate

Before publication: review logs/hashes and matching source commit, complete clean-machine/DAW
acceptance, settle outstanding Windows suite failures, and confirm the release notes name the source
tag and commit. Publish installers only: an asset list a user has to interpret loses downloads.
Obtain explicit owner authorization to publish a release. Developer ID/notarization and Windows
signing can be added later without changing plugin identities or installer UpgradeCode.
