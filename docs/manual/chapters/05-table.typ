#import "../style.typ": *

#chapter(5, "The library table",
  lead: [The table is the catalog seen as rows. Sorting, paging, filtering and the row menu are how you move from a large library to the file you want.],
  inthis: (
    [Columns and what each one means],
    [Sorting the whole catalog, not the page],
    [Page sizes and the virtualized All view],
    [Hide empty, duplicates and status rows],
    [Selection, the bottom bar and the row menu],
  ),
)[

#section("Columns")

#data-table(
  header: ("Column", "Shows"),
  widths: (1fr, 3.4fr),
  rows: (
    ("Sample", "File name. The star at the left toggles the star."),
    ("Type", "Effective instrument/sound type, with markers for overrides and uncertainty (chapter 9)."),
    ("BPM", "Effective tempo. `--` means not analyzed, `?` means analyzed but unknown, `*` means a manual value."),
    ("Key", "Effective musical key: tonic and mode, or only a tonic when no mode is known."),
    ("Genre", "Manual or automatic genre. A manual value is preferred; the Analysis panel names the source."),
    ("Format", "Container extension, lower case."),
    ("Length", "Duration measured from the audio."),
    ("Rate", "Sample rate in kHz."),
    ("CH", "Channel count."),
    ("Tags", "Every tag on the file as a coloured dot and name. Alt-click a tag here to remove it from the file."),
    ("DUPLICATES", "`×N` for a byte-identical group: same full SHA-256 and size."),
    ("Status", "Analyzed, Missing, or the decode error. Ignored rows say so when revealed."),
  ),
)

An em dash means the value is absent; a question mark means the application looked and does not
know. The difference matters: an unknown key is honest, a missing key has not been analyzed.

#shot("/docs/manual/assets/table.png", [Figure 5.1 — The table with the first row selected. The accent stripe marks the selection; the star column is independent of it.], width: 100%)

#section("Sorting and paging")

Clicking a header sorts the whole result, not the current page. Clicking again reverses it. Sorting
by Type, BPM, Key or Genre runs in SQL over the catalog, with unknown values last in both directions
and a stable id tie-break, so a page never shows a random subset of equals.

The page menu offers 100, 1 000, 10 000 and *All*. With *All*, the table is virtualized: only
matching ids are prepared, and records are fetched in small blocks around the viewport. That is how a
300 000-row catalog stays responsive while *All* is selected. Changing a filter or sort with *All*
runs one id query and then reloads blocks, not the whole catalog.

#section("Hide empty")

*Hide empty* hides files whose measured peak is below −60 dBFS (below 0.001), and also rows whose
format this build cannot read. The checkbox is saved between launches and preserved when you switch
catalogs. *Clear filters* turns it off and saves that choice.

Metadata-only rows — RX2, SF2 and similar containers that were indexed without decoding — stay
visible even when they have no measured peak.

#section("Show loops and Show one-shots")

These two checkboxes filter by the structure the analysis confirmed, not by tempo or length. A loop
whose BPM is unknown still passes *Show loops*; a one-shot that carries a tempo still passes
*Show one-shots*.

- Both on: the whole library, including files the analysis has not classified.
- One on: only files confirmed as that category. A phrase or an unanalysed file does not pass.
- Both off: only the rows that are neither, which keeps phrases and unclassified files visible.

Both are saved between launches, and *Clear filters* turns both back on.

#section("Type")

The *Type* pill selects one sample type from the standard list and your custom types, such as
*Snare*. It reads the same value the Type column shows, so a manual Type correction wins over the
analyzer's guess. The pill shows the chosen type (`Type: Snare`) and *All types* in its menu clears
it. The filter combines with tags, search and the structure pair: *Type: Snare* with *Show one-shots*
alone leaves snare one-shots.

Typing `type:snare` in the search field remains the quick way to the same filter, and a typed query
and the pill add up.

#section("Duplicates")

The `DUPLICATES` column groups files that are byte-identical: the same full SHA-256 and the same
size. `×2` means this file has one identical twin in the catalog. Right-click the row and choose
*Show all N copies* to replace the view with the whole group.

Grouping is deliberately strict. Two files that decode to the same audio but differ in container or
metadata are not grouped. Grouping never deletes or moves anything; it is a way to see that a pack
contains the same kick four times.

#section("Selection")

A plain click selects one row and loads it in the preview. Cmd-click (Ctrl-click on Windows) toggles
a row; Shift-click selects a range. Selection is limited to the current page — with *All* that is the
whole result, because *All* is one page.

The bottom bar shows the full path of one selected file and its metadata: Type, Structure, BPM, Key,
Pitch, tags, format, rate, channels, bit depth, duration, star and any missing/error note. With
several rows selected it shows the count and the tags shared by the selection.

Two keyboard notes: Up and Down move through rows and preview as they go when *Auto Preview* is on;
Space plays or pauses the preview, not the table. Escape stops playback and clears a waveform
selection.

#section("The row menu")

Right-click a row (or a multi-row selection) for the actions that apply to files:

- *Copy* — file references on macOS, newline-separated paths elsewhere.
- *Show in Finder* / *Show in Explorer* — reveals the clicked file, not every selected file.
- *Star* / *Unstar* — the same as the star column, for the whole selection.
- *Tag ▸* — assign one tag to the selection. The first entries are the five most recently assigned
  tags in this catalog.
- *Type ▸*, *BPM ▸*, *Genre ▸* — manual corrections, including *Tap BPM…*.
- *Analyze selected*, *AutoTag selected…*, *Reset tags…*.
- *Split by silence…*, *Extract samples…* — chapter 13.
- *Ignore* — hides the path from the library without removing its rows.
- *Show all N copies* — only on a duplicate group.

Chapter 7 covers the tag actions, chapter 8 AutoTag, chapter 9 the metadata submenus. Chapter 21
adds *Batch Processing…* to the same menu.

]
