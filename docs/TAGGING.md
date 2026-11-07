# Tags, recent tags, folder actions and AutoTag

This document describes the tagging stage implemented on schema 8. AutoTag is a rule engine over
the filename and stored metadata. It is not ML, cloud analysis or audio classification, and it
never changes an original file.

## Recent tags

The file context menu starts with up to five recently **assigned** tags, newest first. A quick item
assigns its tag to the whole current selection and never removes an assignment. Opening a menu, a
no-op or a removal does not change the list. Renamed tags show their current name; a deleted tag
drops out.

History lives in the catalog database, so it moves with the database and is isolated per catalog:

```sql
recent_tags(tag_id INTEGER PRIMARY KEY REFERENCES tags(id) ON DELETE CASCADE,
            used_at INTEGER NOT NULL,
            seq INTEGER NOT NULL) WITHOUT ROWID
```

`seq` provides a stable newest-first order even when two assignments share a millisecond. The table
keeps five rows. Only explicit human assignments update it: quick items, the **Tag** submenu, **New
tag**, and **Tag folder**. Bulk AutoTag deliberately does not touch it, so worker batch order cannot
scramble the menu.

## Folder actions

Right-clicking a folder in the sidebar offers:

- **Open in Finder / Explorer** — opens the actual folder. An unavailable path is reported, never
  silently replaced with another location.
- **Scan folder** — scans only that subtree. It adds new files, updates changed ones, retries failed
  ones and skips unchanged ones, preserving ids, tags, stars and overrides. A nested folder is not
  registered as a new imported root, and missing-file reconciliation is restricted to the subtree.
- **Analyze smart metadata** — re-runs the metadata pipeline for every catalogued file below the
  folder. It does not recompute SHA-256 or Phase 1 measured features, does not import files and does
  not change tags.
- **Tag folder...** — confirms the path, inclusion of subfolders, catalogued file count, new
  assignments and already-tagged count before writing. Repeating is idempotent.
- **AutoTag folder...** — the same reviewable preview as selected-file AutoTag, for the subtree.

**All samples** is a virtual node. It has no folder menu: there is no implicit whole-catalog Tag,
AutoTag or filesystem Open.

Every folder scope is resolved in SQL with separator-aware path bounds: `/Samples/Drums` does not
include `/Samples/DrumsExtra`. It is independent of search, tag filters, stars, Hide empty,
duplicate hiding and pagination. Tag/AutoTag/Analyze only act on catalogued rows; Scan is the only
command that discovers files.

## AutoTag

### What it may propose

Only sound/instrument types that already exist as tags in the catalog:

| Class | Exact filename tokens |
|---|---|
| Kick | kick, kicks, kickdrum, kik, bd, bassdrum, bass drum, kick drum, 808 kick |
| Snare | snare, snares, snr, sd, rim, rimshot, sidestick, snare drum |
| Clap | clap, claps, handclap |
| Hat | hat, hats, hihat, hi-hat, hh |
| Percussion | perc, percussion, tom(s), conga, bongo, djembe, tabla, cowbell, woodblock, shaker, tambourine, crash, ride, splash, cymbal(s) |
| Bass | bass, bassline, 808 bass |
| Sub | sub, subbass, sub bass |
| Lead | lead(s), arp, arpeggio, pluck(s), stab, riff |
| Pad | pad(s), atmos, atmosphere, texture(s), drone, ambient |
| Keys | keys, piano, organ, rhodes, wurli, clav |
| Guitar | guitar, gtr |
| Vocal | vocal(s), vox, choir, acapella |
| FX | fx, sfx, effect(s), noise, sweep, riser, impact, foley, downlifter, uplifter |

Compound rules take precedence: **bass drum** is Kick, not Bass plus Kick; **sub bass** is Sub;
**808 kick** is Kick and **808 bass** is Bass. A bare **808** is ambiguous between kick and bass and
is skipped. A bare **drum** is too general and is not a class.

Tokens are matched whole and case-insensitively. `bd` inside another word, `kickstand` and note
numbers do not match. Plurals are accepted for tokens longer than three characters. Loop,
one-shot, BPM, key, mood, genre and structure are outside AutoTag.

The following instrument-like classes have no existing catalog tag and are skipped with
`unsupported-type` rather than invented: synth/chord/poly, brass/trumpet/sax/horn,
strings/violin/cello/viola. Cymbals are mapped to the existing broader Percussion tag.

### Evidence

Accepted evidence:

- filename tokens;
- valid stored Type candidates with a known source (WAV/AIFF metadata, ASD, RX2 and so on);
- a completed, resolved smart Type result from a source other than the folder name.

A folder or pack name is context only. It never establishes a type by itself and cannot veto a type
stated by the filename or metadata. Candidates from an older extractor version, stale smart
results, invalid candidates and changed files are not evidence. Two rows from one source are not
independent confirmations; the engine looks for one unambiguous class, not a vote count.

A manual Type override is never modified. A proposal that differs from it — or a manual
unknown/not-applicable marker — is excluded from application by default and shown in the review
list. Existing tags and assignments are never removed.

Every proposal carries a rule id, a readable reason and a source, for example
`filename token "snare"` or `stored metadata type "kick" from wav_smpl`. There are no confidence
percentages. Competing or conflicting types produce a skip with an explanation.

Rule set version: `autoTagRulesVersion = 1` in `core/tagging/AutoTagTypes.h`.

### Review and application

Preparation runs off the UI thread and writes nothing. The dialog shows the scope, checked files,
proposal and new-assignment counts, already-assigned rows, skipped reasons, no-evidence count and
errors. Proposals are listed with filename, folder, tag, reason and state. Large lists stay in a
virtualized table. Each new proposal can be excluded; already-assigned rows cannot be applied.

**Cancel** closes without writing. **Apply selected** writes only included, still-valid proposals.
Before each batch it revalidates that the sample and tag still exist and that the file size/mtime
matches the snapshotted value; changed or missing files are rejected and counted. Batches are 500
assignments; cancellation rolls back the unfinished batch and keeps completed batches. The result
reports the actual applied and rejected counts.

Budgets: at most 1,000,000 ids in one scope, 50,000 proposals, 10,000 stored skip rows and 500
assignments per transaction. A scope over the limit stops with an explicit message instead of
silently truncating the operation.

## Refresh and limits

After a tag write the library rows, selected tag strip, bottom status line, tag counts and active
filter results refresh. `reloadLibrary()` preserves the selection and does not restart preview
playback.

Known limits:

- Applied AutoTag reasons are not persisted in this stage; they are shown before applying. There is
  no inspector consumer yet.
- Folder names are used as context only, not as proof.
- If a built-in instrument tag is renamed or deleted, AutoTag reports `tag-missing` instead of
  recreating it.
- AutoTag waits for Scan/Smart to finish; folder Tag may run alongside preview.
- Windows/Explorer behavior and physical acceptance remain unverified; macOS Debug is the verified
  platform.
- No ML, cloud analysis, DSP BPM/key/mood/genre tagging or audio classification is implemented.
