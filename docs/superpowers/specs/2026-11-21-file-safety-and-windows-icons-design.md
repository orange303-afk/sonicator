# SONICATOR 0.2 - File deletion, duplicate cleanup and Windows icon verification

Status: approved by the owner for implementation (2026-11-21).

## 1. Purpose

Allow users to remove unwanted physical files safely and reclaim space occupied by byte-identical
duplicates. Independently verify that the Windows application carries its branding on every shell
surface. Issue #4 stays open until a real Windows check is recorded.

Sources:
- GitHub #3: https://github.com/orange303-afk/sonicator/issues/3
- GitHub #4: https://github.com/orange303-afk/sonicator/issues/4
- Owner decisions: separate red deletion command, additional confirmation, Trash-only deletion,
  duplicate cleanup with optional symbolic-link replacement, a Trash/permanent choice for
  overwritten originals.
- Approved duplicate criterion: byte-identical files only.

## 2. Scope

Included: Windows icon audit and acceptance for the published MSI; single and multi-selection
deletion through Trash; a duplicate review window; explicit kept original per group; Trash,
permanent deletion or link replacement; a persisted disposal policy for overwritten originals;
coordination with preview, jobs, indexing and the catalog.

Approach: reuse the existing full-SHA-256 duplicate index, but revalidate bytes before any action.
Use the platform Trash for library deletion and duplicate cleanup. Replace-in-place operations
stage the old file and dispose of it per the explicit policy.

Excluded: permanent library/duplicate deletion except when chosen, secure erase, automatic
background cleanup, audio-similarity matching, hard-link replacement, deleting samples inside
bank/disc containers, recursive folder deletion, removing empty folders, migrating DAW references.

## 3. Constraints

- Some existing operations refuse symbolic links; link replacement must report unsupported
  follow-up operations honestly instead of guessing.
- Exports under `~/Library/Sonicator/exports`, including the drag cache, are project media and are
  never cleaned up by these operations.
- Destructive operations exist only in the standalone application; the plugin shows them disabled.
- Stale catalog hashes are discovery data, never authorization for a destructive action.

## 4. Delete from disk

- Separate context-menu item `Delete from disk...`, destructive red in both themes.
- Selection only; no folder recursion, no container members, no symbolic links, no exports.
- Themed confirmation with count, total size, reviewable paths, skipped reasons and a warning
  about DAW references. Cancel is the default.
- Refuse while a writer or analysis job runs; refuse when the preview holds unsaved edits for an
  affected file.
- Revalidate identity immediately before the move; a changed file is skipped and needs a new
  review.
- Trash only. A failed move keeps the file; no permanent fallback.
- Success removes catalog rows and rebuilds the affected duplicate groups; failures keep their
  rows. Report successful, skipped and failed items.

## 5. Duplicate review

- `Find duplicates...` in the library and row menus; scope is the current catalog.
- Groups come from same size + full SHA-256; every member shows full path and size.
- One kept original per group, changeable before confirmation; the kept copy is never selected.
- Missing, linked and conflicting members are marked and excluded.
- Explicit per-copy selection; an estimate of redundant bytes, labelled as an estimate.
- No cleanup starts from discovery alone.

## 6. Duplicate actions

Trash: as Section 4. Permanent: explicit warning that Trash does not apply. Links: preserve the
path, point directly at the kept original, verify the link, and remove the old bytes only after
verification.

Link recovery: rename the copy to a staging name on the same volume, journal it, create and verify
the link, then delete the staged copy. Any failure restores the real file. On restart, the journal
restores unfinished replacements; ambiguous cases keep both files and name them for review.

Catalog: successful trash/permanent actions remove rows and rebuild only the affected groups;
links stay as usable rows with their metadata. Broken links are reported as unavailable.

## 7. Overwrite disposal

Settings -> Editing, next to `Always overwrite original files`: `Move to Trash` (default) or
`Delete permanently`. Applies only to a replaced original in Save and Normalize. Publish first,
dispose second; a disposal failure keeps the old file and reports its path. Per-job confirmation
states the policy when permanent is selected. Other deletion paths ignore this setting.

## 8. Windows icons

Surfaces: executable at small/medium/large, Start-menu and search entry, desktop shortcut, running
window (title bar, taskbar, Alt+Tab), pins created from Start and from the running app while closed
and after relaunch, Apps & features. Scenarios: clean install on Windows 10/11, upgrade with an
existing pin, repair, Explorer restart or sign-out, 100% and 150% scaling.

Automated checks cover the executable icon, the single MSI icon row, the plain shortcut and the
window's WM_GETICON handles. Manual shell checks are recorded separately; a cache refresh is never
applied automatically.

## 9. Acceptance

- Deletion: single/multi, cancel makes no change, busy/missing/changed/protected/container items
  handled honestly, Trash refusal never permanent, partial failures leave accurate state.
- Duplicates: identical bytes with different names group; same size with different bytes does not;
  stale hashes rejected; selected original never removed; link paths resolve; already-linked files
  excluded; permission, full-disk, missing-original and concurrent-change failures recoverable;
  journal recovery covers interruption.
- Windows: automated checks pass in CI; manual matrix recorded with build number, installer
  version and SHA-256.
- All destructive paths have regression tests on macOS, Windows and Linux; an unverified
  platform/action combination stays disabled with an explanation.

## 11. Follow-up additions (2026-11-21)

- `Find duplicates...` also appears in the folder context menu. A folder scope lists only copies
  inside that folder; it never offers an original outside it.
- Clicking a copy in the review window auditions it through the main preview; a second click
  stops. Missing copies stay silent, unsaved waveform edits are confirmed first, and closing the
  window restores the previous preview source.
- The result reports bytes freed for permanent deletion or verified links, and bytes moved to
  Trash separately (the space returns when Trash is emptied). Catalog row removal and duplicate
  regrouping run on a background thread; an index on `samples.container_path` removes the
  per-path table scan that froze the window after a large cleanup.
- `Ignore symlinks` sits next to `Hide empty` and is also a checkable LIBRARY-menu item (the chip
  yields first at narrow widths). Rows carry an additive `samples.is_link` flag, written on
  analysis and by duplicate link replacement; catalogs written before the column get one
  background pass the first time the option is enabled. The catalog version is not raised, so an
  older 0.1.x build still opens the catalog.
- The standalone checks GitHub once three seconds after launch. Only a newer release opens a
  dialog with `Release notes`, `Download from GitHub` (the platform installer: universal `.pkg`
  first, `.msi`, AppImage before `.deb`) and `Gumroad`. A failed or equal check is silent, the
  dialog waits for an open modal instead of stacking, and the plugin never checks. Settings ->
  About keeps its on-demand check.

## 10. Publication

Public text (issues, commits, docs, release notes) is English. The owner's live library and
exported audio are never used as fixtures.
