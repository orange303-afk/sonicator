#import "../style.typ": *

#chapter(3, "The workspace",
  lead: [Five regions, one window: search and the toolbar above, folders on the left, the library in the middle, the Analysis panel on the right, and the preview across the bottom.],
  inthis: (
    [The header and the search field],
    [The library toolbar and its filters],
    [The folder tree and Favorites],
    [The table, the Analysis card and the preview band],
    [List and Cloud, and resizing the panels],
  ),
)[

Everything in the window is one of those five regions. Nothing hides behind a tab except the
Settings pages and the Cloud view.


#section("Header")

The wordmark is inert. The search field is not: typing filters the library after a 250 ms pause,
Enter applies immediately, Escape clears the query or closes the search history. Clicking the field
opens the last ten searches applied in this catalog.

*Settings* on the right opens the panel described in chapter 23. In the DAW plug-in the same button
opens a reduced Settings with the audio and catalog pages hidden.

#section("Library toolbar")

The first row switches between *List* and *Cloud* and shows the page count: *313 matching | 100 on
page 1* means the filter matches 313 rows and this page shows 100. The arrows move between pages;
the menu next to them sets the page size — 100, 1 000, 10 000 or *All*. *All* is virtualized: it
prepares matching ids and fetches records in blocks while you scroll, so a 300 000-row catalog stays
usable.

The second row holds the filters: *Tags*, *Starred*, *Hide empty* and *Clear filters*. They combine.
*Clear filters* turns off the tag, star and empty-file filters and saves that choice; it does not
clear the search field.

The scan buttons sit above the table: *+ Add folder*, *Rescan*, *New files*, *Reanalyze all* and
*Smart metadata*. Chapter 4 explains when to use which. While a job runs, *Pause* and *Cancel*
appear next to the progress line at the bottom of the window.

#plate("/docs/manual/assets/workspace.png", [Figure 3.1 — The workspace with a scanned library. The header holds search and Settings; the toolbar holds scans and filters; the table is the catalog; the Analysis panel describes the selection; the preview band plays it.])

#section("Folder tree and Favorites")

The left column has two parts. The upper tree shows registered roots and the folders that contain
catalogued files; *All samples* is a virtual node for the whole catalog. Selecting a folder filters
the library to that folder and every descendant. Left and Right collapse and expand the selected
node.

#shot("/docs/manual/assets/folder-tree.png", [Figure 3.2 — The folder tree. Roots are registered by *+ Add folder*; child folders appear when files in them are catalogued. The selected folder filters the table.], width: 28%)

*Favorites* is a separate tree below the divider. A folder added to Favorites stays listed even when
another catalog is open; it is a shortcut across catalogs, not a place where files are copied. The
divider between the two trees can be dragged, and its height is remembered.

#section("The library table")

Each row is one catalogued file. The columns are the sample name, Type, BPM, Key, Genre, Format,
Length, Rate, Channels, Tags, Duplicates and Status. Clicking a header sorts the whole result, not
the current page; clicking again reverses it. Unknown values sort last in both directions.

Selecting a row loads it into the preview. Cmd-click (Ctrl-click on Windows) toggles a row,
Shift-click selects a range. The bottom bar shows the full path of the selection and its metadata;
with more than one row selected it shows the count instead.

#shot("/docs/manual/assets/table.png", [Figure 3.3 — The table. The selected row carries an accent stripe. Tags are coloured dots with their names; the Status column reports analysis results, missing files and decode errors.])

The `DUPLICATES` column shows `×N` when a file is byte-identical to others (same SHA-256 and size).
Right-click a row and choose *Show all copies* to reveal the group. Nothing is deleted by grouping.

#section("Analysis panel")

The right panel describes the selected file. The Key/BPM block shows the two values people look for
first; the card below lists measured features — length, rate, bit depth, peak, RMS, spectral
centroid, rolloff and flatness — and the provenance of smart metadata. Chapter 9 explains the
markers (`--`, `?`, `*`, `!`, `~`) and when a value is honestly unknown.

#section("Panels and the compact layout")

Three dividers resize the workspace: between folders and table, between table and Analysis, and
above the preview. Drag to resize; double-click a divider to restore that panel's default. *Settings
→ General → Workspace → Reset panel layout* restores all of them, but not the window size, the
column widths or your shortcuts.

If the window becomes too small for the panels, they shrink instead of overlapping. Your chosen
sizes return when the window grows again. The DAW plug-in keeps its own panel sizes, separate from
the standalone application.



#block(above: 12pt, below: 12pt, breakable: false)[
  #block(stroke: 0.6pt + rule, radius: 2pt, clip: true)[
    #image("/docs/manual/assets/analysis-card.png", width: 70%)
  ]
  #v(6pt)
  #block(stroke: 0.6pt + rule, radius: 2pt, clip: true)[
    #image("/docs/manual/assets/analysis-hero.png", width: 70%)
  ]
  #v(4pt)
  #set text(size: 8.5pt, fill: muted)
  #set par(leading: 0.6em)
  Figure 3.4 — The Analysis panel for a file with a manual key and tempo. Measured features stay separate from the smart metadata hero below them.
]

For a multi-row selection the panel reports the count and the tags shared by the selection instead
of one file's numbers.

#section("Preview band")

The band under the table is the transport: play, stop, a time display, volume, and the toggles
*Loop*, *Auto Preview*, *MIDI Preview* and *Skip Silence*. Above them sits the waveform. The whole
band is described in chapter 10; chapters 11 and 12 cover selection and editing.

#section("List and Cloud")

*Cloud* replaces the table with a map of the tagged library: one point per tagged file, islands per
tag, hover audition and drag out. The list filter does not apply to the map — it always shows the
whole tagged library — and the camera keeps its position while you switch back and forth. Chapter 14
covers it.


]