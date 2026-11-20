# File safety and Windows icons - implementation plan

Spec: `docs/superpowers/specs/2026-11-21-file-safety-and-windows-icons-design.md`
User guide: `docs/FILE_SAFETY.md`

## Stage 1 - Core primitives

- `core/files/FileDisposal.*`: Trash/permanent policy, link and directory refusal.
- `core/edit/AudioEditWriter.*`: `publishReplacement` stages the old file, publishes the new one,
  restores on failure and disposes of the old copy per policy.
- `core/edit/NormalizeService.*`: policy in options, warnings for a retained old copy.

## Stage 2 - Settings

- `AppSettings::overwriteOriginalDisposal` with JSON round-trip; default Trash.
- Settings -> Editing combo `Replaced originals`, enabled with the overwrite toggle.
- `MainComponent::applySettings` and `PreviewPanel` apply the policy to Save.

## Stage 3 - Delete from disk

- Row menu `Delete from disk...` in `theme::danger`, selection only.
- Eligibility: ordinary files only; refuse containers, links, exports, unsaved edits, busy jobs.
- Themed confirmation, then Trash via `disposeFile`, catalog removal, affected duplicate groups
  rebuilt, List/Cloud/tree/favorites refreshed.

## Stage 4 - Duplicate cleanup

- `SampleRepository::duplicateGroups` and `refreshDuplicateHashes`.
- `core/files/DuplicateCleanup.*`: byte comparison, Trash/permanent/link jobs, staging, journal and
  restart recovery.
- `ui/library/DuplicateCleanupDialog.*`: group and member lists, per-copy checkboxes, kept
  original, action choice, progress, results.
- Library menu and row menu `Find duplicates...`; result applied to the catalog by MainComponent.

## Stage 5 - Windows icons

- `packaging/tests/test_release.py`: one MSI icon row, ARPPRODUCTICON, plain shortcut targeting the
  executable, branding PNG feeding the executable icon, builder requiring the generated ICO.
- `packaging/windows/test-install.ps1`: associated icon at least 32 px, WM_GETICON large and small
  handles on the running window, Start entry AppID printed.
- `docs/RELEASE_INSTALLERS.md`: manual shell matrix (pins, Alt+Tab, upgrade, repair, scaling,
  Explorer restart) and the rule that a cache refresh is never applied automatically.

## Stage 7 - Follow-up additions

- Folder context menu `Find duplicates...` scoped to copies inside the folder.
- `Ignore symlinks` chip and LIBRARY-menu toggle; `samples.is_link` additive column, analyzer and
  cleanup writes, one-time background pass with a catalog-local marker.
- Launch update check with Release notes, platform download and Gumroad, guarded against stacking
  on a modal; platform asset selection tested without a network.
- Duplicate cleanup reports bytes freed (permanent/link) and bytes moved to Trash separately;
  catalog row removal and duplicate regrouping run off the message thread. The freeze after a
  large cleanup was `removeByPaths` scanning `samples` per path for `container_path`: an additive
  index (`samples_container`, created after the column exists) removes the scan.

## Stage 6 - Verification

- `tests/TestFileSafety.cpp`: policy round-trip, disposal refusals, Normalize replacement without
  staging files, verified duplicate removal, link replacement, journal recovery, group shrinking.
- Build the macOS app and run the relevant test tags.
- Record Windows/Linux native results separately; an unverified platform/action stays disabled.
