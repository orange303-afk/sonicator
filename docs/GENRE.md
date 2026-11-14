# Genre column

Date: 2026-11-14. Schema `user_version` 17.

## What it is

`Genre` is a **musical genre** — House, Hip-Hop, Ambient — not a sample type (`Kick`, `Pad`), not a
structure (`loop`) and not an instrument. The library column shows the effective value; the
Analysis card shows the value and where it came from.

## Where the value comes from

Sources are tried in this order, and every one is stored as its own candidate:

1. **Metadata written by the exporter** — RIFF INFO `IGNR`, ID3 `TCON`, AIFF `cate` when the value
   is a genre. Source ids: `info`, `id3`, `aiff_cate`.
2. **The nearest folder** — only the two folders directly above the file. A library root, a drive
   or a volume name is never consulted, so `/Volumes/1TBSSD/samples/...` assigns nothing.
3. **The file name** — whole tokens only: `trap_kick_01.wav` matches Trap, `housekeeper.wav` does
   not match House.

The dictionary lives in `core/smart/GenreContextExtractor.cpp`; `genreDictionaryVersion` is bumped
when the vocabulary or the matching rules change.

## Resolution and conflicts

- A manual value always wins and is never overwritten by analysis.
- An explicit **Unknown** blocks automatic guessing until the override is cleared.
- Metadata outranks a folder, a folder outranks the file name.
- Two **name-based** sources that disagree (folder says House, file says Trap) leave the column
  empty with state `Ambiguous` and keep both candidates in the explanation. A metadata value that
  disagrees with a folder keeps the metadata value; the folder is still named in the explanation.
- Genre is never derived from BPM, key or sample type, and a genre is never written into the Type
  column (builds before schema 17 did exactly that; a reanalysis clears the old Type value).

## Editing

Right-click one or more rows → **Genre ▶**:

- **a genre name** – sets a manual value for every selected row;
- **Unknown** – records "deliberately unknown", distinct from "not detected";
- **Clear manual genre** – returns to the automatic value;
- **+ Custom genre…** – free text, stored as written.

Assigning a genre changes only the genre; tags, stars, Type, BPM and key are untouched.

### Assigning a whole folder

Right-click a folder in the library tree → **Set genre... ▶** → a genre name or
**+ Custom genre...**. This assigns a manual Genre to catalogued files in the folder and
**all its subfolders**, regardless of the table's search, tag/star filters, duplicate view or page.
Ignored paths are excluded; both members of a merged mono stereo pair are included.

If any file has a different manual genre (including explicit Unknown or not applicable), a
**Replace manual genres?** dialog shows the folder, file count and conflict count. Nothing has
been changed yet. **Replace** applies the genre to the whole scope; **Cancel** changes nothing.
Files already carrying the same manual genre do not need replacement.

The catalog operation runs in the background and is atomic: a database error rolls back the
whole assignment. Original audio files are never edited. The action also works with catalogued
folders that are temporarily offline; the virtual **All samples** node is not a target.

This is a **one-time assignment**, not a folder inheritance rule: newly added files receive no
manual genre from this action. Clear a file's override through its row menu to return to automatic
inference. Automatic folder/name matching is unchanged.

## Querying

- Column `GENRE`, sortable; empty values sort last.
- Search token `genre:house` matches `House` and `Deep House` (case-insensitive substring);
  `genre:!trap` excludes. The filter uses the manual value when one is set.

## Storage

Schema 17 adds four columns to `smart_results` (`genre`, `genre_state`, `genre_source`,
`genre_override`). They are added with `ensureColumn`, so a schema-16 catalog upgrades in place
with a backup written first; IDs, tags, stars, Type/BPM/Key and existing overrides are untouched.
Rows analysed by an older extractor become `stale` and are recomputed by the next smart pass, not
by the upgrade itself.

## Measured cost

The sort/filter uses the same correlated-subquery shape as the existing Type sort. On the owner's
catalog (375 828 rows) the full ordered query with `LIMIT 1000` took **2.19 s cold / 0.35 s warm**
with the Type expression (the live catalog is still schema 16, so the identical `genre` expression
could not be run before the app migrates it). Audio is never decoded for this query.

## Not verified

Windows and a manual pass with Russian folder names on a real library. The corpus test covers nine
synthetic layouts; the dictionary is small and will need extension from real mistakes.
