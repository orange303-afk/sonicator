#import "../style.typ": *

#heading(level: 1)[Appendix E · Glossary]

#data-table(
  header: ("Term", "Meaning in this manual"),
  widths: (1.5fr, 3fr),
  rows: (
    ("Catalog", "The SQLite database that indexes your files. It stores paths and derived data, not audio."),
    ("Root", "A folder registered with *+ Add folder*; the unit for Rescan, New files and Tag folder."),
    ("Metadata-only row", "A catalog row for a container (RX2, SF2, image entry) that is not decoded until selected."),
    ("Measured value", "A number computed from decoded audio: length, rate, peak, RMS, centroid, rolloff, flatness."),
    ("Smart metadata", "Type, BPM, Key, Genre, Structure and Pitch, resolved from metadata, names and optional DSP."),
    ("Override", "A manual correction of a smart value; the automatic value stays underneath."),
    ("Ambiguous", "The evidence supports more than one answer, so no single value is published."),
    ("Stale (`~`)", "A result computed before a dependency or analyzer version changed; still visible, no longer current."),
    ("Duplicate group", "Files with the same full SHA-256 and size; shown as `×N`, never deleted."),
    ("Stereo pair", "Mono L/R files recognised as one stereo source in the list and preview."),
    ("Prepared selection", "A WAV written from a waveform range, ready to drag. Not an edit of the original."),
    ("Edit buffer", "The in-memory copy the waveform editor changes; the file changes only on Save."),
    ("Tag island", "A cluster on the cloud map; one tagged file is one point."),
    ("Batch chain", "The ordered list of processing steps in a Batch job."),
    ("Match BPM / Match Scale", "Plug-in toggles that render a tempo- or key-matched copy of the selected loop."),
    ("REPORT", "The button that opens a prefilled GitHub issue in your browser; nothing is sent automatically."),
  ),
)
