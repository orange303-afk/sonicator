#import "../style.typ": *

#chapter(20, "Dragging and exporting",
  lead: [A drag is a copy, never a move. This chapter is the practical side of getting audio into a project and keeping that project working after the session.],
  inthis: (
    [Drag one file, several files or a selection],
    [What the DAW receives],
    [Where prepared WAVs live and how long they last],
    [Keeping a project's references alive],
  ),
)[

#section("Three kinds of drag")

1. *A library row* carries the original file. The DAW imports its own copy into the project, or Finder
   copies it. The library original stays where it is.
2. *Several rows* carry every selected existing original in table order. Missing files are skipped.
3. *A waveform selection* carries a newly written WAV of exactly that range. The original is not
   touched and the export remains under `exports/`.

The first two are file URLs; the third is also a file URL, just a different file. Nothing in the
application ever hands a DAW a stream, a pointer or a temporary in-memory object.

#section("Into the DAW")

Drop the drag on a track, a sampler slot or the arrangement. Most DAWs import the file and copy it into
the project folder; some reference it in place. SONICATOR cannot control which policy a host uses, so
the rule is simple: originals are never moved by the drag, and a DAW that references them can find
them later as long as you keep the library folder where it is.

A selection WAV is the safer choice when you want a permanent slice in the project: it lives under
`exports/` with a normal path, and the project can reference it like any other file. The source file
can be re-edited without changing what the project already imported.

#section("Where prepared files live")

Selection WAVs and drag-cache files live together under:

- macOS: `~/Library/Sonicator/exports/`, with drag cache under `exports/drag/`.
- Windows: `%APPDATA%\Sonicator\exports\`.

Nothing is deleted automatically. A project or a sent message may hold that path, so cleanup is your
decision. If you delete an export that a project references, the project loses the audio, exactly as
it would with any missing file in your sample folder.

#tip[
  Treat `exports/` as project media, not as a cache. If you need a clean-up, do it per project: close
  the project, find the WAVs it uses, and remove only the exports you recognise.
]

#section("Prepared drag cache")

An EBL sample cannot be dragged as a file at all. SONICATOR materialises a WAV in `exports/drag/`
first. Disc entries and tracker samples are different: a disc entry drags only as a waveform
selection, and a tracker source is prepared by its *Drag sample* button; neither becomes a row path. The file name includes a hash
of the source path, so dragging the same sample twice reuses one file and a project that referenced it
keeps working when the application re-renders it.

A failed conversion is reported in the status line and does not drop the rest of a multi-file drag.

#section("Session hygiene")

- Close the DAW before upgrading SONICATOR. The plug-in and the application share a catalog, and an
  upgrade can migrate it once (chapter 2).
- Keep the catalog and the samples on the same machine or drive where possible; *Relocate folders…*
  exists for the day they move (chapter 23).
- If a file is missing in the library, fix it there first. A DAW project cannot repair a path that the
  catalog already lost.
- A selection is prepared in the background. If the status still says *Preparing selection…*, wait a
  moment instead of dragging; a refused drag is clearer than a truncated file.

#note[
  External drag is verified on macOS. On Windows the same JUCE file-URL path is used, but drag has not
  been verified there; test it with your host before depending on it in a session.
]

]
