#import "../style.typ": *

#chapter(25, "When something goes wrong",
  lead: [Most problems leave a message and a log line. This chapter tells you where to look, what to send, and which problems are actually expected behaviour.],
  inthis: (
    [The log and the crash file],
    [How to send a useful report],
    [Common situations and their answers],
    [Backups, restore and privacy],
  ),
)[

#section("The log")

SONICATOR writes a rolling log:

- macOS: `~/Library/Logs/Sonicator/sonicator.log`
- Windows: `%APPDATA%\Sonicator\logs\sonicator.log`

The file rotates at 2 MB into a single `sonicator.1.log`. A crash writes `crash.txt` beside the log
with the signal and a backtrace; the next report includes it.

Log lines name files and folders, because a diagnostic without the failing file is rarely actionable.
They do not contain audio.

#section("A useful report")

*Settings → REPORT* opens a new GitHub issue with the template and environment already filled in: a
bug report, a feature request, or a format/bank support request. The body holds the version, OS and
the last log lines. Your home folder becomes `~`, the catalog path becomes `<catalog>` and your account
name becomes `<user>`; file names stay.

Read the body before submitting. Remove anything private, add the smallest sample that reproduces the
problem, and say what you expected. Nothing is sent automatically; the issue opens in your browser and
you press Submit.

#section("Common situations")

*The library is empty after launch.* The saved catalog may be missing or may not be a SONICATOR
catalog. The application falls back to the default location and writes the fallback to the log. Open
*Settings → Open catalog…* and choose the right file.

*Folders are red or missing.* A root is unavailable: an unplugged drive, a renamed folder, another
computer. Reconnect it and rescan, or use *Relocate folders…*. Records are never deleted by a failed
root.

*A file plays but shows no waveform or metadata.* The container may be metadata-only (RX2, SF2,
instruments, disc entries) until selected, or the decoder may have refused it. The Status column and
the Analysis panel name the reason.

*A file is silent.* *Hide empty* may be hiding it; turn the filter off. If it is visible and silent,
the audio may really be silent, or the output device may be wrong (*Settings → Audio & MIDI*).

*MIDI Preview does nothing.* Check that an input is enabled in *Settings → Audio & MIDI*, that the
track sends to the plug-in, and that the sample is loaded. The transport line reports `voices N · MIDI
<note> ok` or `no zone`; a note outside an instrument's map is silent by design.

*The scan is slow.* A first import hashes and measures every file. A large drive takes time; Pause and
Cancel are safe and keep completed results. Later rescans skip unchanged files.

*Smart metadata stays `?`.* Some values are genuinely unknown: ambiguous tempo, relative keys, drums
without pitch. Chapter 9 explains when to correct and when to leave it.

*The plug-in says the library is older or newer.* The application owns the schema. Open the
application once to migrate it, then reopen the project. The plug-in never migrates and never writes a
backup.

*`exports/` keeps growing.* Selection WAVs and drag cache are project media and are never deleted
automatically. Remove only files you recognise; see chapter 20.

*I turned on always-overwrite and lost an edit.* *Settings → General → Editing* controls it. The
editor's temporary file and atomic replace protect against a failed write, but an intentional Overwrite
is intentional. Backups are the recovery point.

#section("Backups and restore")

*Settings → Backup database* writes a consistent copy to `Sonicator/backups/`. To restore, quit the
application, keep the current catalog and its `-wal`/`-shm` files aside, copy the backup over
`sonicator.sqlite`, and start the application. The catalog is an index: a backup restores tags, stars,
metadata and measured features, not the audio files themselves.

A relocation writes `<catalog>.relocate-<timestamp>.sqlite` before changing paths. That file is the
recovery point for a wrong mapping.

#section("Privacy in one paragraph")

Three actions can use the network: the update check you press, the report you submit yourself, and
the optional contribution that is off by default. Everything else — indexing, search, tagging,
analysis, preview, editing, export — works offline. Chapter 23 lists exactly what the contribution
sends; *Delete my contributed data* removes it and issues a new installation id.

]
