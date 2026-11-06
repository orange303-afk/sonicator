# Search and tags — implementation plan

Spec: `docs/superpowers/specs/2026-11-06-search-tags-design.md`

- [x] 1. Schema v3: `search_key` column with backfill, `tags`/`sample_tags` tables, standard seed
- [x] 2. Repository: `LibraryFilter`/`LibraryPage`, global query with text/folder/tags/sort, tag CRUD and per-sample tags
- [x] 3. Database tests: search semantics, wildcard escaping, tag combinations, assignment survival, v2→v3 migration
- [x] 4. UI primitives: `TagStrip`, `TAGS` column, header sort callback, tag filter menu
- [x] 5. MainComponent: debounced global search, filter state, pagination, tag editing
- [x] 6. UI/integration tests and 300k search timing
- [x] 7. Docs, full build, CTest, graphify update

## Notes

- Search is synchronous with a 250 ms debounce because the measured 300k query is 14–25 ms.
- No analysis version bump: stored DSP output does not change.
- Migration keeps the existing online backup and must not rewrite valid analysis rows.
