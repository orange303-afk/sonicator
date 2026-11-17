#import "style.typ": *

#set document(
  title: "SONICATOR User Manual",
  author: "Ilya Orange",
  keywords: ("sample browser", "SONICATOR", "user manual"),
)

#let version = "0.1.0"
#let build-date = "November 2026"

#show: book

#cover(version: version, commit: "November 2026")

#page(footer: none, numbering: none)[
  #set text(size: 9.5pt)
  #v(60mm)
  #text(size: 20pt, weight: "bold")[SONICATOR User Manual]
  #v(10pt)
  #text(fill: muted)[
    Version #version. Written for the standalone application on macOS and Windows
    and for the SONICATOR VST3/AU plug-in.
  ]
  #v(20pt)
  #line(length: 100%, stroke: 0.5pt + rule)
  #v(10pt)
  #grid(
    columns: (1fr, 1fr),
    gutter: 16pt,
    [
      *Copyright* \
      © 2026 Ilya Orange.
    ],
    [
      *Licence* \
      SONICATOR is free software under the GNU Affero General Public License v3.0 or
      later. This manual is distributed under the same licence.
    ],
  )
  #v(10pt)
  #grid(
    columns: (1fr, 1fr),
    gutter: 16pt,
    [
      *Sources* \
      `github.com/orange303-afk/sonicator`
    ],
    [
      *Built* \
      #build-date · Typst · Manrope
    ],
  )
  #v(14pt)
  #text(size: 8.5pt, fill: muted)[
    This book describes the build named above. Menu labels, defaults and limits can change
    between releases; when a label in the application differs from the book, the application
    is right.
  ]
]

// ---- How to read ----------------------------------------------------------

#heading(level: 1, outlined: false)[How to read this manual]

This is a working manual, not a feature catalogue. Each chapter starts with what it covers and
ends with the state you should see in the application. Most chapters can be read on their own, but
Parts I and II build on each other: the catalog, its folders and its tags are the ground everything
else stands on.

#heading(level: 2, outlined: false)[Where to start]

If you have just installed SONICATOR, read chapters 1–3, add one folder, and stop there until the
first scan finishes. Then continue with chapters 4–9 on a second session. The rest of the book is
reference for the day you need it.

If you already have a catalog, the two chapters people reach for most are 6 (search) and 7
(tags). Chapter 25 covers backups and what to do when something looks wrong.

#heading(level: 2, outlined: false)[Keys]

Keys are written as words in small boxes: #keys("Cmd", "S") on macOS, #keys("Ctrl", "S") on
Windows. Where the platforms differ, both are given. The application uses Command on macOS and
Control on Windows throughout; it never translates one into the other.

The screenshots were taken on macOS in the dark theme. Windows uses the same controls in the same
places; only the window frame and the key names differ.

#heading(level: 2, outlined: false)[Labels and paths]

Interface labels are printed in *bold* exactly as they appear: *Add folder*, *Hide empty*,
*Reanalyze all*. Search syntax, file names and paths are printed in `monospace`. Menu paths read
top to bottom: *Settings → Hotkeys* means open *Settings*, then choose *Hotkeys*.

#heading(level: 2, outlined: false)[Honest limits]

Some formats open only as metadata, some values stay unknown, and some features have been verified
on macOS only. Those limits are stated where they matter, not collected at the end. When the
application does not know something, the manual says so too.

// ---- Contents -------------------------------------------------------------

#heading(level: 1, outlined: false)[Contents]

#outline(title: none, depth: 1, indent: auto)

// ---- Chapters -------------------------------------------------------------

#part(1, "Getting started", "What the application is, how to install it, and how the window is organised.")

#include "chapters/01-what-it-is.typ"
#include "chapters/02-installing.typ"
#include "chapters/03-workspace.typ"
#part(2, "Building the library", "Folders, scans, the table, search, tags and the metadata that makes the library useful.")

#include "chapters/04-folders.typ"
#include "chapters/05-table.typ"
#include "chapters/06-search.typ"
#include "chapters/07-tags.typ"
#include "chapters/08-autotag.typ"
#include "chapters/09-metadata.typ"
#part(3, "Listening and shaping", "Audition, select, edit, split and explore the library as a map.")

#include "chapters/10-preview.typ"
#include "chapters/11-selection.typ"
#include "chapters/12-editor.typ"
#include "chapters/13-split-extract.typ"
#include "chapters/14-cloud.typ"
#part(4, "Special sources", "Loops, instruments, tracker modules, legacy banks and disc images.")

#include "chapters/15-rex-sf2.typ"
#include "chapters/16-instruments.typ"
#include "chapters/17-trackers.typ"
#include "chapters/18-legacy.typ"
#include "chapters/19-disc.typ"
#part(5, "Into the DAW", "Get audio out of SONICATOR and into a project, in bulk or from the plug-in.")

#include "chapters/20-dragging.typ"
#include "chapters/21-batch.typ"
#include "chapters/22-plugin.typ"
#part(6, "Keeping it healthy", "Settings, keys, backups and the day something goes wrong.")

#include "chapters/23-settings.typ"
#include "chapters/24-hotkeys.typ"
#include "chapters/25-troubleshooting.typ"

#include "appendices/A-shortcuts.typ"
#include "appendices/B-search.typ"
#include "appendices/C-formats.typ"
#include "appendices/D-files.typ"
#include "appendices/E-glossary.typ"

// ---- Back cover -----------------------------------------------------------

#page(
  footer: none,
  background: rect(fill: plate-dark, width: 100%, height: 100%),
  margin: (top: 40mm, bottom: 30mm, x: 26mm),
)[
  #set text(fill: white)
  #text(size: 10pt, fill: accent, weight: "bold", tracking: 2pt)[SONICATOR]
  #v(16pt)
  #text(size: 15pt, weight: "bold")[
    A local sample browser for macOS and Windows.
  ]
  #v(10pt)
  #text(size: 11pt, fill: rgb("#c9cbcf"))[
    Index the folders you already have, tag and correct them, audition and edit without touching
    the originals, and drag the result into a DAW. No account, no cloud library, and three optional
    network actions you start yourself.
  ]
  #v(18pt)
  #line(length: 30mm, stroke: 1.2pt + accent)
  #v(14pt)
  #text(size: 9pt, fill: rgb("#a3a5a9"))[
    Free software under the GNU Affero General Public License v3.0 or later. \
    © 2026 Ilya Orange. \
    `github.com/orange303-afk/sonicator`
  ]
]
