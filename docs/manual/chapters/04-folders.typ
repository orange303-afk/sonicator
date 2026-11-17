#import "../style.typ": *

#chapter(4, "Folders and scanning",
  lead: [The catalog mirrors folders; it does not own them. This chapter covers adding roots, the five kinds of scan, and what happens when a folder moves or disappears.],
  inthis: (
    [Add a root and understand what a root is],
    [Pause, cancel and resume a scan],
    [Rescan, New files, Reanalyze all, Smart metadata and Analyze selected],
    [Missing roots, error rows and Remove from library],
    [Folder actions, ignored paths and the packs SONICATOR registers itself],
  ),
)[

#section("Roots and folders")

*+ Add folder* registers a folder as a root and indexes everything below it, recursively. The root
stays visible in the tree even when it is empty. Subfolders appear only when they contain catalogued
files; an empty directory is not discovered.

A root is the unit for several actions. *Rescan* walks that root, *New files* adds what is new below
it, *Tag folder* applies a tag to everything catalogued below it. If you add one root per pack, those
actions stay precise; if you add your whole drive as one root, they become broad.

Two kinds of pack are registered automatically on macOS the first time they are found:

- Reason Packs: `~/Library/Application Support/Propellerhead Software/Packs`. WAV, AIFF and RX2 files
  are indexed normally; `_audio previews` and `Audio Previews` folders are skipped. A global setting
  stops the pack returning after you remove it.
- Ableton Factory Packs: `~/Music/Ableton/Factory Packs`. Playable WAV/AIFF and the rendered OGG
  previews under `Ableton Folder Info/Previews` are indexed; presets and `.asd` sidecars are not.

Samples inside Ableton packs that use the closed `able` codec are skipped by design: only Live can
decode them, so they are neither catalogued nor turned into error rows.

#section("The first scan and its controls")

A scan reads each file's header, hashes its bytes and measures it. The toolbar shows the current
path and a counter. While a job runs:

- *Pause* stops between files and keeps the position.
- *Resume* continues from the same point.
- *Cancel* stops the job and keeps every completed result. A file that was being written is not
  stored, so the catalog never contains a half-analyzed row.

Completed results survive cancellation; the next *Rescan* picks up the rest. The catalog is written
through SQLite transactions, so closing the application mid-scan does not leave a corrupt catalog,
though the unfinished files are simply not there yet.

#section("Five ways to scan")

The scan buttons look similar and do different things. Choose by what changed.

#data-table(
  header: ("Action", "What it does", "Use it when"),
  widths: (1.2fr, 2.4fr, 1.8fr),
  rows: (
    ("*Rescan*", "Walks the registered roots; analyzes new, changed and previously failed files; skips unchanged analyzed files. Reconciles missing records.", "Files were added, edited or deleted inside an existing root."),
    ("*New files*", "Walks the folders and analyzes only paths that are not in the catalog. Does not mark missing records or rebuild image contents.", "You only added files and want the fastest pass."),
    ("*Reanalyze all*", "Recomputes every catalogued file after a stronger confirmation.", "After an analysis change, or to repair a damaged result."),
    ("*Smart metadata*", "Reads Type/BPM/Key/Genre candidates for missing, changed or old-version files, and runs the rhythm/tonal fallback when no metadata tempo exists.", "The smart columns are empty or stale; no audio analysis pass is needed."),
    ("*Analyze selected*", "Queues the chosen rows without stopping a running job.", "A handful of files failed or look wrong."),
  ),
)

*Rescan* and *Reanalyze all* ask for confirmation because they can take a long time. *New files* is
additive: it deliberately does not reconcile missing files. If a file disappeared, only *Rescan*
marks it missing.

#section("Missing roots, missing files and errors")

If a root itself is unavailable — an unplugged drive, a renamed folder — the scan stops with a
visible message and changes nothing. Records are not deleted.

A file that disappeared inside an available root is marked *Missing* in the Status column. Its tags,
stars and metadata stay. Put the file back and the next *Rescan* reconnects it by path. A file that
cannot be decoded keeps an error status with the reason; *Rescan* retries failed files. Nothing is
deleted automatically.

#tip[
  Missing files are a warning, not a cleanup tool. If you really want rows gone, remove the folder
  from the library (below), or delete the file and rescan — but a catalog with a few missing rows is
  usually better than a catalog that forgot your tags.
]

#section("Remove from library")

Right-click a folder in the sidebar and choose *Remove from library…*. This deletes that folder's
catalog rows, tags and analysis — from the catalog, not from disk. The confirmation names the folder
and explains the distinction. A removed folder also stays out of later scans, including a parent
root's rescan, until you add it again with *+ Add folder*.

Removing a root unregisters it. Removing a subfolder leaves the root registered and removes only that
subtree.

#section("Folder actions")

The folder menu carries the actions that make sense for a subtree:

- *Open in Finder* / *Open in Explorer* opens the real folder.
- *Scan folder* scans only that subtree: new, changed and failed files; unchanged files are skipped.
- *Analyze smart metadata* re-runs the metadata pipeline for catalogued files below the folder
  without hashing or importing anything.
- *Tag folder…* assigns one tag to the catalogued files below the folder, optionally including subfolders,
  after a confirmation that names the counts.
- *AutoTag folder…* runs the reviewable AutoTag pass for the subtree (chapter 8).
- *Add to favorites* pins the folder in the Favorites tree.

*All samples* is virtual and has no folder menu: there is no implicit whole-catalog tag or scan.

#section("Ignored paths")

A folder can be ignored instead of removed. An ignored path keeps its catalog rows but hides them
from the library. This is useful for a parent pack whose `_previews` folder you never
want to see. *Clear filters* does not reveal ignored paths; the library has a separate *show ignored*
state, and *Ignore* is reversible from the same folder menu.

]
