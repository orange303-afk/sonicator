# Context menu, stars and window geometry — implementation plan

Spec: `docs/superpowers/specs/2026-11-06-context-menu-design.md`

- [x] 1. Schema v4 `starred`, batch star API, starred filter, star-preserving upsert
- [x] 2. Multi-select list, selection preservation by ID, star column
- [x] 3. Context menu: Copy, Tag submenu with colour dots and ticks, Star/Unstar, Open in Finder/Explorer
- [x] 4. Native macOS file clipboard with text-path fallback
- [x] 5. Starred filter button and clear-filters integration
- [x] 6. Window geometry in settings, merge-safe updates, display validation and restore
- [x] 7. Tests, docs, Debug/Release, screenshots, graphify update

## Notes

- Analysis upserts never update `starred`, so favourites survive rescan and reanalysis.
- Windows `Copy` falls back to paths until `CF_HDROP` support is added.
