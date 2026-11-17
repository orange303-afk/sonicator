#import "../style.typ": *

#chapter(1, "What SONICATOR is",
  lead: [A local browser for a sample library: it indexes the files you already have, lets you find and audition them, and hands them to a DAW without moving anything.],
  inthis: (
    [What the catalog is, and what it is not],
    [The non-destructive promise],
    [What works offline, and the three actions that use the network],
    [What the application deliberately does not do],
  ),
)[

SONICATOR is a sample browser. Point it at the folders where your samples live — a single pack, a
drive of libraries, an old sampler collection — and it builds an index of what is there: names,
formats, lengths, measured audio features, tags you assign, and metadata it can read or infer.

The important word is *index*. The catalog is a database of what the files look like, not a copy of
them. Browsing, searching, tagging, previewing and dragging never move, rename, rewrite or delete an
original. The one exception is explicit and always confirmed: the waveform editor can save over a
file, but only after you choose *Overwrite* in its save dialog. Everything else writes to SONICATOR's
own folders.

#section("Files and the catalog")

The application keeps three things apart:

#block(
  fill: tint,
  inset: 12pt,
  radius: 3pt,
  above: 10pt,
  below: 14pt,
)[
  #set text(size: 9.5pt)
  #grid(
    columns: (1fr, auto, 1fr, auto, 1fr),
    column-gutter: 8pt,
    align: (center + horizon, center + horizon, center + horizon, center + horizon, center + horizon),
    [*Your folders* \ audio files, untouched], [→], [*The catalog* \ one SQLite database], [→],
    [*What you see* \ library, search, tags, map],
  )
]

The left column is the only place audio lives. The catalog records a path, a hash, measured values
and everything you add: tags, stars, metadata corrections, analysis results. Delete the catalog and
your samples are still there; point the application at the same folders and it can index them again.

Because the catalog stores paths, moving samples behind its back is the one thing that breaks the
picture. Chapter 4 explains how a scan reconciles it and chapter 23 explains *Relocate folders…* for
a library that has moved to another drive or computer.

#section("Works offline, with three visible exceptions")

Indexing, search, tagging, analysis, preview, editing and export work with no network connection and
no account. Three actions can use the network, and each is started by you:

#data-table(
  header: ("Action", "Where", "What leaves the computer"),
  widths: (1.1fr, 1.1fr, 2.4fr),
  rows: (
    ("Check for updates", "Settings → About", "The application version, in the request header. No catalog, path or account data."),
    ("Report a problem", "Settings → REPORT", "A prefilled GitHub issue: version, OS, the last log lines, a crash trace. It opens in your browser; you submit it."),
    ("Contribute anonymous feedback", "Settings → Library", "Off by default. When on: coarse acoustic measurements, confirmed type/tags, key and tempo, an installation id — never audio, names, paths or hashes."),
  ),
)

Chapter 23 covers each one in detail, including what the contribution preview shows and how to
delete submitted data. Nothing is sent silently, and the update check never runs on its own.

#section("What the application does not do")

Being clear about the edges saves time:

- It is not a sample manager that moves files into a pack structure. Folders stay where you put
  them; tags and stars live in the catalog.
- It does not rewrite metadata in your files by browsing. Only the waveform editor can write audio,
  and only through its save dialog.
- It is not a DAW and does not host plug-ins on a track. It produces file drags and prepared WAVs;
  the DAW does the recording.
- It does not use machine learning to invent tags from audio. Smart metadata is rule- and
  measurement-based and refuses to guess when evidence conflicts (chapter 9).
- It does not watch folders in the background. New files are found by a scan you start (chapter 4).

The rest of this part gets you installed and shows how the window is arranged. Part II turns a
folder into a library worth searching.

]
