# Context menu, stars and window geometry — design

Stage after Settings/About. Scope: right-click actions on library rows, Cmd/Shift multi-selection,
persisted favourites, and remembered window position/size.

## Menu

Right-click on a row selects it if it was not selected and opens:

```text
Copy                 selected existing files to the clipboard
Tag                  submenu of all tags with colour dots and ticks, then New tag...
Star / Unstar        applies to the whole selection
Open in Finder       Explorer on Windows, folder on other platforms
```

## Decisions

- **Multi-selection** uses JUCE `TableListBox` native behaviour: Cmd toggles, Shift extends from
  the lead row. Selection is saved by stable sample ID before a refresh and restored afterwards.
  Scope is the current 10,000-row page.
- **Tag ticks** mean the tag is present on every selected sample. A mixed selection has no tick;
  clicking adds the tag to all. Batch assignment runs in one transaction. `New tag...` creates and
  assigns to the whole selection. Colour markers are 14 px rendered dot images in the menu items.
- **Star** is schema-v4 `samples.starred`, not a tag. Analysis upserts omit it from the conflict
  update, so rescan and reanalysis cannot clear a favourite. The table gains a narrow star column
  and the filter bar a `Starred` toggle that combines with text, folder and tags.
- **Copy** writes real file URLs through `NSPasteboard` on macOS (`ui/platform/FileClipboard.mm`);
  other platforms write newline-separated paths (`FileClipboard.cpp`). Missing files are skipped;
  the item is disabled when nothing exists.
- **Open** reveals the clicked row's file with `File::revealToUser()`; a missing file reveals its
  parent folder. On Windows JUCE maps this to Explorer. Multiple folders are not opened at once.
- **Window geometry** lives in `settings.json` (`windowX/Y/Width/Height`). A `SettingsStore::update`
  load-modify-save method prevents the window writer and the theme/catalog writers from erasing
  each other's fields. Geometry is written after a 500 ms move/resize debounce and at shutdown.
  On launch, saved bounds are validated against connected displays, clamped to 880×580 and the
  current work area, and off-screen bounds are recentred. The interface scale is applied first.

## Non-goals

- No drag-and-drop, no favourites page, no copy on Windows as file references yet, no per-row
  mixed-tag indicator in the menu, no cross-page selection.

## Verification

Schema v4 migration and star defaults; batch stars and the starred filter; star survival through
an analysis upsert; batch tags; multi-selection preserved across a sorted refresh; starred filter
in the real shell; window-bound resolver (inside, off-screen, too small, too large); independent
settings updates. Native clipboard contents and the popup menu are checked manually.
