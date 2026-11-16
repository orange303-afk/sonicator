# Release installers (unsigned)

SONICATOR currently ships without Developer ID/notarization or Windows Authenticode.
An installer build is not the same as a verified public release. Actual checks and
remaining limits are in [RELEASE_INSTALLERS_ACCEPTANCE.md](RELEASE_INSTALLERS_ACCEPTANCE.md).

## Deliverables

- `SONICATOR-0.1.0-macOS-universal.pkg`: independently selectable application, VST3 and AU;
  arm64 + x86_64, macOS 11 or newer.
- `SONICATOR-0.1.0-Windows-x64.msi`: independently selectable application and VST3;
  64-bit Windows 10 or newer. AU is macOS-only.
- `SONICATOR-0.1.0-sources.tar.gz`: matching project, pinned dependency sources, build and
  LGPL relinking instructions. Distribute it alongside the corresponding installer.
- SHA256 checksums and `source-commit.txt` in workflow artifacts.

Windows and macOS source archives contain their own platform's ONNX runtime package (and
Windows zlib sources where fetched). They have the same filename but platform-specific content;
keep each with its platform's installer instead of overwriting one with the other.

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
outside iCloud; final installers go in ignored `dist/`. Python 3.11+ is needed by source-archive
creation (developer tooling only). Product operation needs no Python.

Locations:

- `/Applications/SONICATOR.app`
- `/Library/Audio/Plug-Ins/VST3/SONICATOR.vst3`
- `/Library/Audio/Plug-Ins/Components/SONICATOR.component`

The app and plugins embed ONNX under `Contents/Frameworks`, with relative load commands.
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

The release preset serializes MSBuild projects and sets `SONICATOR_MSVC_COMPILER_JOBS=2` for
parallel translation-unit compilation. CI selects 4 workers only with at least 12 GiB RAM.
Other presets retain their original compiler-parallelism behavior.

Never run `packaging/windows/test-install.ps1` on a working user's installation. It is guarded
for a disposable GitHub runner, where it exercises installed app window creation, pluginval,
repair, a higher-version upgrade fixture, feature selections and uninstall, while checking
catalog/settings/export hashes. The temporary 0.1.1 MSI is a **test fixture**, not a release.
The disposable startup profile explicitly opts out of contribution sharing, avoiding the
first-launch consent modal during the shutdown test. It refuses to overwrite an existing
profile; interacting with the first-launch dialog is separate manual acceptance.

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
acceptance, settle outstanding Windows suite failures, and include matching sources and notices.
Obtain explicit owner authorization to publish a release. Developer ID/notarization and Windows
signing can be added later without changing plugin identities or installer UpgradeCode.
