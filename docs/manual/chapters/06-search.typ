#import "../style.typ": *

#chapter(6, "Search",
  lead: [One field searches the whole catalog: names and paths first, then formats, types, keys, notes and genres when you ask for them with a prefix.],
  inthis: (
    [What a plain word matches],
    [Masks and format filters],
    [Type, key, note and genre filters],
    [Quotes, alternation and combinations],
    [History, timing and what Enter does],
  ),
)[

Typing filters after a 250 ms pause. Enter applies the query immediately; Escape clears it, or
closes an open history menu. Clicking the field opens the last ten applied searches in this catalog;
choosing one applies it. The history is stored in `settings.json`, so it survives a restart and does
not travel with the catalog.


#section("Plain words")

A bare word matches the file name and the full path. Several words are combined with *and*: every
token must appear. Matching folds case for Latin, Greek and Cyrillic; scripts without case (CJK)
match literally.

Because the full path is included, searching for a pack or a folder name works without any prefix.
A word that happens to be a known instrument type also checks smart metadata: `pad` finds files typed
as Pad even when the name does not contain the word. The same applies to a note: `C4` finds files
whose resolved sampler root is C4.

#plate("/docs/manual/assets/search.png", [Figure 6.1 — `type:kick key:Am` narrowed a 313-row catalog to one file. The count line reads *1 matching | 1 on page 1*.])

#section("Masks and formats")

A mask matches the *complete* file name. `*.rx2` finds every REX2 file; `kick_*` finds names that
start with `kick_`. Masks are not substring searches: `kick_*` does not match `my_kick_01.wav`.

Format filters are shorter and combine with commas:

#data-table(
  header: ("Example", "Matches"),
  widths: (1.4fr, 3fr),
  rows: (
    ("`ext:wav`", "Files whose container is WAV."),
    ("`ext:rx2,sf2`", "REX2 or SoundFont files."),
    ("`*.rex/rx2/sf2`", "The compact form of the same idea: three masks in one token."),
    ("`*.rx2|*.sf2`", "Alternation: either mask. Spaces around the bar are allowed."),
  ),
)

#section("Type, key, note and genre")

These prefixes ask smart metadata, not the file name:

#data-table(
  header: ("Example", "Matches"),
  widths: (1.4fr, 3fr),
  rows: (
    ("`type:kick`", "Effective type Kick. A manual Type wins; an automatic resolved type counts too."),
    ("`key:Am`", "Effective key A minor. `key:A` matches any A key regardless of mode."),
    ("`note:C4`", "A resolved sampler root note of C4, not the defaults many banks write."),
    ("`genre:house`", "The effective genre contains `house`. A manual genre wins over an automatic one."),
    ("`type:pad genre:ambient`", "Both conditions at once."),
  ),
)

A value the application could not resolve does not match these prefixes. That is intentional: an
ambiguous tempo is not silently treated as one of its candidates.

#section("Quotes and exact words")

Quotes make the search exact. `"rimshot"` matches that word, not `rim` inside `rimshot`; `"deep
house"` matches the phrase as a whole. Use quotes when a short token would otherwise match too much.

#section("Combining, sorting and clearing")

Everything combines with *and*; there is no separate expression language. Format, type, key and genre
tokens can sit next to words in one query. Header clicks sort the whole filtered result, not just the
visible page.

*Clear filters* clears the tag, star and empty-file filters; it does not clear the search text. The ×
inside the field clears the query. Escape clears the query when the field has focus, otherwise it
stops the preview and clears a waveform selection.

#tip[
  Save a long query by applying it once: it enters the ten-item history. The history is per catalog
  location because it lives in the settings file, not in the database.
]


]