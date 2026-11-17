#import "../style.typ": *

#chapter(9, "Metadata and corrections",
  lead: [Type, tempo, key, genre and structure come from metadata, names and measurement. You can correct any of them; the automatic value stays visible underneath.],
  inthis: (
    [Which values are smart, and which are measured],
    [The markers in the columns],
    [Correcting Type, BPM and Genre],
    [Tap BPM and the learned prototype],
    [Why some values stay unknown],
  ),
)[

#section("Two layers")

The Analysis panel mixes two kinds of information, and they are not interchangeable:

- *Measured* values come from decoding the audio: duration, sample rate, channels, bit depth, peak,
  RMS, spectral centroid, rolloff and flatness. They are facts about the file.
- *Smart* values come from metadata chunks, sidecars, names and optional DSP passes: Type, BPM, Key,
  Genre, Structure and Pitch. They are candidates with a source and a state, and they can be wrong or
  unknown.

A correction changes the smart value, never the measured ones. Reanalyzing does not erase a manual
correction, and a rescan does not remove it.

#section("Markers")

#data-table(
  header: ("Marker", "Meaning"),
  widths: (1fr, 3.4fr),
  rows: (
    ("`--`", "Not analyzed yet."),
    ("`?`", "Analyzed, but the value is unknown or ambiguous."),
    ("`*`", "A manual value is in effect."),
    ("`!`", "Sources disagree."),
    ("`~`", "Stale: a dependency or the analyzer version changed since the result."),
    ("`edited unknown` / `n/a`", "You explicitly marked the value unknown or not applicable."),
  ),
)

The bottom selection bar adds Structure (`loop` or `one-shot`) and Pitch, which the table does not
show. Key and Pitch are different fields: a sampler root note never becomes a musical key, and a key
never becomes a pitch.

#section("Correcting Type")

Right-click a row and open *Type ▸*:

- a standard type assigns it to the selection;
- a custom type is recorded as learned evidence for the classifier;
- *Reset* removes the override and returns to the automatic value.

A manual Type is never overwritten by AutoTag or by a later analysis. It is also the value that
`type:` search and AutoTag use as the effective type.

#section("Correcting BPM")

*BPM ▸* offers *Set BPM…*, *Tap BPM…* and *Clear manual BPM*. Tap BPM opens a small dialog: tap
Space in time, Return applies the average, Escape cancels. Values are limited to 20–300 BPM.

A manual BPM wins over metadata and DSP. The automatic value remains visible in the stored result, so
clearing the override brings it back. Half-time and double-time readings are marked ambiguous rather
than forced to one number.

#section("Correcting Genre")

*Genre ▸* assigns a genre, *Unknown*, *+ Custom genre…*, and *Clear manual genre* removes the
override. The Analysis panel names the source next to the value: manual, automatic or ambiguous. A
genre typed by hand is stored in the catalog and can be used with `genre:` search. The Genre column
is not a Type: genre is its own field.

#section("Structure and Pitch")

Structure and Pitch are informational and appear in the selection bar. Structure is a probable
`loop` or `one-shot`; for a rhythmic loop the DSP may add a BPM, but an ambiguous tempo is reported as
such. Pitch is a one-shot estimate from the tonal pass, given as a MIDI note and cents when the file
is harmonic enough; drums, chords and noise stay unknown.

#section("Why values stay unknown")

Unknown is a valid result, not a failure:

- Tempo can be genuinely ambiguous (half/double-time, variable tempo, no clear pulse).
- Relative keys (C major vs A minor) are reported ambiguous rather than picked arbitrarily.
- A tonic without a mode never gains an invented mode.
- Enharmonic spellings (`C#` / `Db`) are not conflicts.
- Drums, non-harmonic hits, chords and very short files often have no meaningful key or pitch.
- A container may carry no tempo metadata, and the rhythm pass may refuse to guess.

When the catalog shows `?`, the right move is usually a manual correction if you know the answer, or
leaving it unknown. AutoTag and `type:` search use the effective value and report an ambiguous one as
ambiguous rather than inventing a different answer.

#section("What runs when")

*Smart metadata* in the toolbar runs the metadata pipeline for missing, changed or old-version files.
Only when no usable metadata BPM exists does it decode the audio for a local rhythm pass; that pass
also adds the probable Structure. A tonal pass supplies key and one-shot pitch. Everything runs
locally — no network, no model download.

The catalog stores candidates, extractions and the resolved result separately. A reanalysis can
therefore use stored candidates without re-reading audio, and a stale result is marked `~` rather
than silently pretending to be current.

]
