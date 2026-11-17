#import "../style.typ": *

#chapter(14, "The cloud map",
  lead: [Cloud is a second view of the library: one point per tagged file, islands per tag, and a camera you can zoom into. It answers "what do I have in this corner?" rather than "where is this file?".],
  inthis: (
    [What is drawn, and what is left out],
    [Hover audition and the caption],
    [Camera: zoom, pan, Back and Overview],
    [Selecting, dragging and the row menu],
    [Why the list filter does not apply],
  ),
)[

Switch with *List* / *Cloud* in the library toolbar. The choice is saved in `settings.json` and restored on the next
launch.


#section("What is on the map")

One point is one file that has at least one tag. Untagged files are not on the map — there is no
island to put them in. A file with two tags sits between two islands, and the bridges are often the
most interesting part of the picture.

Tag islands are laid out by how often tags appear together and packed tightly, so the map reads as
one cloud rather than a ring of separate discs. A tag with one file still gets a visible point; a
large tag does not swallow the map. The island size scales with the square root of its file count.

Inside an island, files are placed by measured timbre where those measurements exist: RMS, peak,
spectral centroid, rolloff and flatness. Duration, sample rate and channel count are deliberately
excluded — thousands of files share the same one-second length, and including it would line them up
in stripes. Files without measurements sit at the centre of their island, and files with identical
vectors are spread on a spiral instead of overlapping.

#plate("/docs/manual/assets/cloud.png", [Figure 14.1 — The cloud map in overview. Islands are tags; points are files. The map always shows the whole tagged library, not the current list filter.])

#section("Hover")

Hovering a point shows its file name, a soft halo, and plays the first ten seconds of the file.
Playback has a 10 ms ramp and is silenced while the main preview is playing, so the two never talk
over each other.

Some points cannot be auditioned: REX2, EXS24, NKI, SF2, Akai and tracker files are not opened by
the shared decoder used for hover, so hover is silent. Dragging, selecting and the context menu still
work.

#section("The camera")

- Mouse wheel zooms towards the cursor.
- Dragging the background pans.
- *Back* returns through the last 20 camera positions.
- *Overview* fits the whole map.
- Double-clicking an island zooms to it.

The camera keeps its position when you switch to List and back, and when a filter changes. The map
does not rebuild on a filter change: filtering is a property of the list, not of the layout.

Labels name the ten largest islands in overview, skip overlaps, and fade in after the camera stops.
Zooming in reveals the remaining island labels; hovering a point shows its file name.

#section("Selecting and dragging")

A click selects the file through the same path as a list row, including the unsaved-edits prompt if
the preview holds edits. If the file's row is not on the loaded list page, the list stays without a
selection rather than substituting another file.

Dragging a point out of the window starts the same non-destructive file drag as a row: a copy for
Finder or a messenger, an import for a DAW. The original is never moved. Right-clicking a point opens
the ordinary row menu.

#section("Limits")

- The map is about tags, not a global projection of sound. Neighbourhood detail is sharp only where
  the 84-value descriptor vector exists; elsewhere the island's base descriptors and the tag carry
  the position.
- About 5% of points have no audio position and say so on hover.
- The layout is deterministic and cached; changing a filter does not move anything, changing tags
  does. A rebuild runs in the background and publishes only its own generation.
- Group selection on the map is not implemented; the row menu and drag act on one point at a time.


]