# File deletion, duplicate cleanup and overwrite disposal

Three related operations change files on disk. They share one rule: nothing is deleted through a
symbolic link, and nothing is deleted permanently unless the person chose that explicitly.

## Delete from disk

The row context menu has a separate red `Delete from disk...` item, for one selected row or a
multi-selection. It is not `Remove from library...`: the catalog row and the file both go.

- Only ordinary files are eligible. Rows inside a bank or disc image, symbolic links and files
  under `~/Library/Sonicator/exports` are refused and never widen the selection to a folder or
  container.
- A confirmation lists the count, the total size and the paths, and says plainly that DAW projects
  may reference them. Cancel is the safe default.
- The files go to the platform Trash / Recycle Bin through `juce::File::moveToTrash()`. A failed
  move keeps the file and is reported; there is no fallback to permanent deletion.
- Files with unsaved edits in the preview are refused until the edits are saved or discarded.
- The operation waits while a scan, smart analysis, Split, Extract, Batch or Normalize job runs.
- After the moves, the catalog rows are removed, the affected duplicate groups are rebuilt, and
  the List, Cloud, folder tree and favorites refresh. Restored files reappear after a rescan; their
  tags are not restored automatically.

Implementation: `ui/MainComponent.cpp` (`deleteFilesFromDisk`, `performDeleteFromDisk`) and
`core/files/FileDisposal.cpp`.

## Duplicate review and cleanup

`Find duplicates...` in the library menu, the row menu and the folder context menu opens the
review window. A folder scope only lists copies inside that folder, so a cross-folder decision is
never made on the person's behalf. Groups come from the existing byte-identical index: same file
size and full SHA-256. Names, tags and audio descriptors are never evidence of duplication.

- The window shows every group, every member's full path and size, and one kept original per
  group, which can be changed before starting.
- Clicking a copy auditions it through the main preview (an accent stripe marks the sounding
  copy); a second click stops it. Auditioning asks about unsaved waveform edits first, and closing
  the window restores whatever the preview showed before. Missing copies are not auditioned.
- Copies that are already symbolic links, missing, or inside a group without a usable original are
  marked and cannot be selected. The kept original is never selected for cleanup.
- Every copy is compared byte for byte with the kept original again immediately before the action.
  A file that changed, moved or disappeared is skipped.
- The result reports the size of permanently deleted copies and of the old bytes removed after a
  verified link as `Freed ...`; bytes moved to Trash are reported separately, because the disk
  space returns only when the Trash is emptied.
- The catalog rows are removed and the affected duplicate groups rebuilt on a background thread;
  the window shows `Updating the catalog...` and then the summary, so a large group cannot freeze
  the UI. A `samples.container_path` index keeps the row removal from scanning the table per path.
- Actions:
  - `Move to Trash` - recoverable.
  - `Delete permanently` - confirmed explicitly for the whole selection.
  - `Replace with links` - the old path becomes a symbolic link to the kept original.

### Link replacement

The copy is renamed beside its path (`.<name>.sonicator-cleanup-<uuid>`), the link is created at
the original path, and only after the link is verified to resolve to the kept original are the old
bytes removed. A failure at any step restores the real file. A small journal in
`~/Library/Sonicator/duplicate-cleanup.json` records the staging path before anything moves; the
next run of the window (or the next cleanup) restores half-finished replacements.

Limits that the confirmation states: moving or deleting the kept original later breaks every link
to it, and some software does not follow symbolic links. On Windows, creating a symbolic link needs
Developer Mode or the create-symbolic-link privilege; when it is refused the copy is restored and
the failure is reported, without elevation prompts and without falling back to hard links.

Implementation: `core/files/DuplicateCleanup.*`, `ui/library/DuplicateCleanupDialog.*`.

## Hiding symbolic links

The library toolbar has `Ignore symlinks` next to `Hide empty` (also a checkable item in the
LIBRARY menu, where it stays reachable on narrow windows). It hides rows whose path is a symbolic
link, including copies replaced by links in duplicate cleanup. `samples.is_link` is written when a
file is analysed and by the cleanup itself; a catalog written before that column existed gets one
background pass over its paths the first time the option is enabled, with progress in the status
line. The column is additive: the catalog schema version is not raised, so an older 0.1.x build can
still open the same catalog and simply ignores the column.

## Replaced originals: Trash or permanent

Settings -> General -> EDITING, next to `Always overwrite original files`, has a `Replaced
originals` choice:

- `Move to Trash` (default).
- `Delete permanently`.

It applies only to an overwrite of an original file: Save in the editor and Normalize with
`Overwrite originals`. The old file is renamed aside first, the new file is published, and only
then is the old one moved to Trash or deleted. A failed publication restores the old file. If
disposal fails, the save is still reported as successful and the retained copy's path is shown.

Even with `Always overwrite original files` on, a permanent disposal still shows one explicit
warning before the file is replaced; the overwrite shortcut never hides an irreversible delete.
The setting never applies to `Delete from disk...` or to duplicate cleanup, which carry their own
confirmation. New and existing profiles default to Trash.

Implementation: `core/edit/AudioEditWriter.cpp` (`publishReplacement`), `NormalizeService.cpp`,
`ui/settings/SettingsPanel.cpp`, `core/settings/AppSettings.h`.

## Tests

`tests/TestFileSafety.cpp` covers the policy round-trip, permanent disposal, link refusal, a
Normalize replacement without staging files, verified duplicate removal, link replacement, journal
recovery, and shrinking duplicate groups. Destructive tests use disposable fixtures only, never
the owner's library or exported audio.
