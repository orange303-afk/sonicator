# SONICATOR — recent tags, AutoTag and folder actions

Status: next-stage specification, not implemented. Derived from the user's approved
scope in the previous session. Remaining policy choices below are proposals, not
claims of user approval. Read `docs/phase2-progress.md` for the current code handoff.

## Goal

Make manual tagging fast and add conservative, explainable instrument-only AutoTag
for selected files and recursive folder contents. Original files remain unchanged.

## Scope

- Last five assigned tags directly at the top of the file context menu.
- AutoTag for selected catalog entries and for folders with subfolders.
- Folder context menu: Scan, Analyze, Open in Finder/Explorer, Tag, AutoTag.
- AutoTag proposal review before application, background operations and cancellation.
- No ML, cloud analysis, new DSP, genre/mood/structure/BPM/key tagging or renaming.

## Recent tags

- Five unique most recently successfully assigned tags, newest first.
- Persist across launches, scoped to the catalog and preserved when its DB moves.
- Store tag IDs, display current names/colours; remove deleted tags from history.
- A quick item assigns to all selected files; it never toggles assignments off.
- Existing assignments are skipped without duplicate rows.
- Opening a menu, removing a tag or performing a no-op does not update history.
- Empty history has no empty section; keep existing menu actions below a separator.

## Operation scope

- Selected files: exact selected sample IDs, not all search results.
- Folder Tag/AutoTag/Analyze: all catalog entries inside that folder and subfolders,
  regardless of search, tags, stars, Hide empty, duplicate filtering or pagination.
- Path matching must respect separator boundaries: Drums does not include DrumsExtra.
- Only Scan discovers and imports files not yet catalogued.
- Show affected counts before bulk assignments; do not silently extend an approved scope.
- All samples is virtual, not a filesystem folder; do not expose implicit whole-catalog
  bulk tagging through its context menu in this stage.

## Folder commands

### Scan

Recursively scan only the chosen subtree. Add new files, update changed ones, retry
failed ones according to existing indexing policy, skip unchanged ones. Preserve IDs,
tags, stars and overrides. Missing reconciliation is restricted to that subtree;
an unavailable root must not mark the entire subtree missing. Scanning a child folder
must not register it as a new imported root.

### Analyze

Repeat smart metadata analysis for catalog entries in the subtree. No SHA-256/basic
feature recomputation, tag mutation or import. Preserve overrides. Refresh displayed
results without restarting playback. Do not describe this as ML or DSP analysis.

### Open

Open the actual folder in Finder/Explorer (or the platform file manager); report an
unavailable path explicitly rather than silently opening a different location.

### Tag

Choose an existing tag and assign it recursively. Confirm folder, inclusion of
subfolders, total entries and new assignments. Repeat is idempotent, never removal.

### AutoTag

Prepare the same reviewable proposals as selected-file AutoTag, for the full subtree.

## AutoTag rules

User confirmed all supported instrument types, not only pad/bd/snare/bass.
Use an explicit instrument whitelist mapped onto existing catalog tags, not every
user-created tag. Preserve existing tag identities and names unless explicitly agreed.

- pad/pads -> Pad; bd/kick/bass drum -> Kick; snare/sd -> Snare; bass -> Bass.
- Extend to other instrument categories through a documented, tested synonym dictionary.
- Loop/One-shot, mood, genre, BPM and key are outside this stage.
- Use filename tokens and suitable existing instrument metadata/smart candidates.
- Parent folder/pack name alone cannot establish an instrument type.
- Match whole tokens; bd embedded inside a word is not Kick.
- Compound rules take precedence: bass drum is Kick, not automatically Bass + Kick.
- Stale/invalid candidates cannot count as current evidence.
- Repeated copies of one source are not independent confirmations.
- Competing plausible types are skipped with an explanation, not assigned together.
- Never invent confidence percentages or classify audio by sound without an algorithm.
- Existing tags are never deleted; Type overrides are never changed. Proposals that
  contradict a manual Type override are excluded by default and shown for review.
- Rules are pure/deterministic and versioned, separate from UI and SQLite.

## Review and application

Preparation writes no tag assignments. Show scope, checked files, proposals, new
assignments, already assigned entries, ambiguities and errors. Each proposal shows
filename/full path, tag, reason/source and an include/exclude control. Large lists
must be virtualized. Apply selected writes only enabled proposals; Cancel writes none.

Freeze catalog and target scope at preparation. Revalidate changed dependencies,
deleted entries and deleted tags before applying. Newly discovered files are not
silently included. Catalog switches invalidate/cancel the old operation.

Bulk work runs off the UI/audio thread, with bounded memory and transaction batches.
Cancelling application rolls back the unfinished batch but retains completed batches;
report the actual applied count. One file failure must not silently abort everything.
Progress shows operation, stage, processed count, errors and current target; do not
fabricate percentages when the total is unknown. Avoid unnecessary full SSD walks.

Refresh row tags, selected tags, bottom bar, tag counts and active filter results.
Do not restart preview or reset transport as a side effect of refreshing tags.

## Storage and unresolved implementation decisions

Use existing SampleRepository, FolderTree, IndexingService and SmartAnalysisService;
extract substantial new tagging logic out of MainComponent rather than growing it
indefinitely. Migrate with the existing SQLite backup mechanism, preserving all user
data. Pick the schema number only after inspecting actual HEAD/schema.

Before implementation fix the exact instrument whitelist/synonyms, AutoTag's policy
for updating recent-tag history, source attribution for applied AutoTag assignments,
batch/memory/cancellation budgets and handling incompatible simultaneous jobs.
Recommended history policy: only explicit human tag assignments update MRU, so a
bulk AutoTag cannot scramble it in worker execution order. This remains a proposal.

## Tests and acceptance

- MRU uniqueness, cap of five, ordering, no-op/removal behaviour, rename/delete,
  persistence and catalog isolation.
- Recursive scope, adjacent-prefix paths, filters/pagination independence, empty and
  unavailable folders, no extra imported root on child Scan.
- Analyze preserves overrides and does not recompute measured features/hash.
- Synonyms/token boundaries/case/Unicode, bass drum, conflicts, missing evidence,
  pack-only names, invalid/stale metadata and manual override contradictions.
- Preview Cancel writes none; Apply is idempotent and preserves existing tags.
- Cancellation between batches, per-file errors, deleted tags, changed files and
  catalog switches; bounded memory and responsive playback.
- Existing search, Hide empty/preferences, MIDI/SF2/REX2 preview, waveform export and
  drag-and-drop remain working. Windows acceptance is separate from macOS results.

## Delivery order

1. Recent tags and persistence.
2. Folder Open/Scan/Analyze.
3. Recursive tag assignment.
4. Tested AutoTag rules and proposal model.
5. Review UI and safe application.
6. Performance/cancellation/regressions, documentation and graphify update.

Deliver independently testable slices. Do not implement later smart DSP/inspector
work under this task. Report unknowns honestly rather than claiming total coverage.
