#import "../style.typ": *

#heading(level: 1)[Appendix D · Files and folders SONICATOR writes]

All paths are inside JUCE's per-user application-data directory: `~/Library/Sonicator/` on macOS,
`%APPDATA%\Sonicator\` on Windows. The log is the exception: it lives under `~/Library/Logs/Sonicator/`
on macOS.

#data-table(
  header: ("Path", "What it is", "Safe to delete?"),
  widths: (2.2fr, 2.4fr, 1.6fr),
  rows: (
    ("`sonicator.sqlite`", "The catalog: rows, tags, stars, metadata, measured features.", "No — back it up instead."),
    ("`sonicator.sqlite-wal`, `-shm`", "Live SQLite sidecars.", "No, not while the application runs."),
    ("`settings.json`", "Theme, scale, catalog path, window geometry, Hide empty, searches, shortcuts.", "Yes — settings return to defaults."),
    ("`backups/`", "Manual catalog copies.", "Only copies you no longer need."),
    ("`<catalog>.relocate-<timestamp>.sqlite`", "A pre-relocation backup beside the catalog.", "After a successful relocation, when you are sure."),
    ("`exports/`", "Selection WAVs and prepared drag audio.", "Only files no project or message references."),
    ("`exports/drag/`", "Materialised WAVs for EBL, disc and tracker drags.", "Same rule: project media."),
    ("`exports/match/<key>/`", "Matched BPM/Scale renders from the plug-in.", "Same rule: project media."),
    ("`cache/cloud/`", "At most two cloud-layout files.", "Yes — rebuilt on demand."),
    ("`batch-presets.json`", "Your batch chains and output settings.", "Yes — presets are lost."),
    ("`batch-reports/`", "At most 20 diagnostic batch reports, 30 days.", "Yes."),
    ("`sonicator.log`, `sonicator.1.log`", "Rolling log.", "Yes, while the application is closed."),
    ("`crash.txt`", "Last crash marker and backtrace.", "Yes, after sending a report."),
  ),
)

#section("What is never written")

Original audio is opened read-only. Browsing, scanning, tagging, analysis, preview and selection
export do not change it. The waveform editor writes only when you save, and *Normalize selected* writes
only after its confirmation or when *Always overwrite original files* is on. Split and Extract write
new files; they move an original to Trash only if you ticked that option.

A DAW project that references a file in `exports/` will fail to load that audio if you delete it. Treat
the folder as project media, not as a cache.
