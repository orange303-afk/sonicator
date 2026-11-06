# Hide duplicates — implementation plan

Spec: `docs/superpowers/specs/2026-11-06-hide-duplicates-design.md`

- [x] 1. Schema v5 `duplicates` table and migration
- [x] 2. Grouping by full SHA-256 + size with representative selection
- [x] 3. Query filters: `hideDuplicates` and one `duplicateHash` group
- [x] 4. Duplicate counts, info and members for the list and inspector
- [x] 5. Refresh after indexing, startup, catalog switch and starring
- [x] 6. UI: Hide duplicates toggle, COPIES column, inspector paths, Show all copies
- [x] 7. Tests, screenshots, docs, Debug/Release, graphify update

## Notes

- Byte equality only; decoded-PCM comparison is a later, opt-in stage.
- The representative is catalog-wide; the escape hatch clears filters for that group.
