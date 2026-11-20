#import "../style.typ": *

#chapter(2, "Installing and first launch",
  lead: [Three installers, all unsigned. Here is what macOS and Windows will say about that, where the application keeps its data, and how to get the first folder indexed.],
  inthis: (
    [What the macOS and Windows installers contain],
    [Getting past Gatekeeper and SmartScreen without disabling them],
    [Where the catalog, settings, logs and exports live],
    [The first folder and the first scan],
  ),
)[

#section("What is in the package")

The release ships three installers and a source archive for each build. They are built from the
same commit; keep each installer with the source archive for its build.

#data-table(
  header: ("Artifact", "Platform", "Contents"),
  widths: (2.2fr, 1fr, 2.2fr),
  rows: (
    ("`SONICATOR-0.2.0-macOS-universal.pkg`", "macOS 11 or newer", "Application, VST3 and AU, arm64 and x86_64"),
    ("`SONICATOR-0.2.0-macOS-x86_64-10.15.pkg`", "macOS 10.15, Intel only", "Application, VST3 and AU for Macs that cannot install the universal package; no ONNX runtime"),
    ("`SONICATOR-0.2.0-Windows-x64.msi`", "Windows 10 or newer, 64-bit", "Application and VST3 (AU is macOS-only)"),
    ("`SONICATOR-0.2.0-sources.tar.gz`", "Universal and Windows builds", "Project and pinned dependency sources"),
    ("`SONICATOR-0.2.0-sources-x86_64-10.15.tar.gz`", "Catalina build", "Project and pinned dependency sources for the Intel-only package"),
  ),
)

The Catalina package leaves out ONNX Runtime, whose bundled dylib needs macOS 13.3. The application
and plug-ins work normally; the optional neural classifier falls back to the built-in acoustic rules
on that machine.

Every artifact has a `.sha256` file. If you downloaded the files by hand, compare the checksum
before installing:

```sh
shasum -a 256 SONICATOR-0.2.0-macOS-universal.pkg     # macOS 11+
shasum -a 256 SONICATOR-0.2.0-macOS-x86_64-10.15.pkg  # macOS 10.15, Intel
certutil -hashfile SONICATOR-0.2.0-Windows-x64.msi SHA256   # Windows
```

Both installers are *unsigned and not notarised*. That is a fact about this build, not a mistake in
your download. macOS and Windows will warn you; the next two sections get past the warning without
turning protection off.

#section("macOS: the package and Gatekeeper")

Open the `.pkg` and follow Installer. The package lets you choose the application, the VST3 and the
AU separately. The default installs all three:

- `/Applications/SONICATOR.app`
- `/Library/Audio/Plug-Ins/VST3/SONICATOR.vst3`
- `/Library/Audio/Plug-Ins/Components/SONICATOR.component`

Because the package is unsigned, macOS may refuse to open it before its scripts run. After the first
attempt, open *System Settings → Privacy & Security* and look for an *Open Anyway* button for
SONICATOR; the exact wording depends on the macOS version. If the system offers no such button, use
the Installer's own authorisation dialog rather than disabling Gatekeeper globally.

#warning[
  Do not run `spctl --master-disable` or add a blanket exception. The package is ad-hoc signed, not
  Developer ID signed, and the safe path is the per-app approval above.
]

The package removes the quarantine flag from the components it installed. It does not touch other
users' files, other plug-ins or global security settings.

Quit SONICATOR and any DAW before upgrading. Open the application once after installation: it
creates or migrates the shared catalog. The plug-in deliberately never migrates the catalog, so a
DAW that opens first will show an explanation instead. Restart the host (or rescan plug-ins) after
installing. Older per-user copies of the plug-in are not deleted; if a host lists two versions,
remove the one you no longer use.

#section("Windows: the MSI and SmartScreen")

Run the `.msi`. The installer is per-machine and lets you select the application and the VST3. The
default locations are:

- `C:\Program Files\SONICATOR\`
- `C:\Program Files\Common Files\VST3\SONICATOR.vst3`

Windows may show *Windows protected your PC* because the installer has no Authenticode signature.
Choose *More info → Run anyway*; the installer then proceeds normally. Do not disable SmartScreen
for the whole system.

The MSI supports repair, reinstall of the same version, major upgrade and uninstall. It refuses a
downgrade. The application carries its own ONNX runtime and Visual C++ runtime next to the
executable; it does not install a system-wide redistributable.

#section("Where SONICATOR keeps its files")

The application writes to JUCE's per-user application-data directory:

#data-table(
  header: ("Item", "macOS", "Windows"),
  widths: (1fr, 2.1fr, 2.1fr),
  rows: (
    ("Catalog", "`~/Library/Sonicator/sonicator.sqlite`", "`%APPDATA%\\Sonicator\\sonicator.sqlite`"),
    ("Settings", "`~/Library/Sonicator/settings.json`", "`%APPDATA%\\Sonicator\\settings.json`"),
    ("Log", "`~/Library/Logs/Sonicator/sonicator.log`", "`%APPDATA%\\Sonicator\\logs\\sonicator.log`"),
    ("Exports", "`~/Library/Sonicator/exports/`", "`%APPDATA%\\Sonicator\\exports\\`"),
    ("Backups", "`~/Library/Sonicator/backups/`", "`%APPDATA%\\Sonicator\\backups\\`"),
  ),
)

The catalog is a live SQLite database with `-wal` and `-shm` sidecars. Do not delete or copy those
files by hand while the application is running; use *Backup database* in Settings (chapter 23),
which takes a consistent copy through SQLite's own API.

#section("First launch and the first folder")

The first window is empty: no catalog rows, one *All samples* node and a *Library* root. Click
*LIBRARY → Add folder…* and choose a folder. One import registers that folder as a root and walks everything
below it recursively.

Indexing runs in the background. The toolbar shows the current file and a counter; *Pause*,
*Resume* and *Cancel* control the job. Cancel keeps every file that finished; a partial file is never
stored. On a first import of a large drive this can take a while — the application reads each file's
header, hashes its bytes and measures it.

#tip[
  Add one root per logical library rather than one per album. A root is a unit for *Rescan*, *Rescan and analyze new files* and *Tag folder*; a hundred tiny roots make those actions tedious.
]

When the scan finishes, the table fills and the status line reads how many samples are loaded. You
now have an installed application, a catalog in the default location and one indexed root. Chapter 3
walks the window; chapter 4 covers the other scan modes and what to do when a folder moves.

]
