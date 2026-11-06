# Page sizes and virtual all view — implementation plan

Spec: `docs/superpowers/specs/2026-11-06-page-sizes-virtual-design.md`

- [x] 1. Page-size setting and persisted dropdown (100 / 1 000 / 10 000 / All)
- [x] 2. `queryIds` and `recordsByIds` repository APIs with shared filter SQL
- [x] 3. Virtual FileListPanel mode: row count from IDs, block loads, metadata merge, 4000-row cache
- [x] 4. MainComponent wiring, count/footer states, indexing refresh policy, selection/scroll retention
- [x] 5. Tests: persistence, ordering, blocks, selection, shell switching and paging
- [x] 6. Scale measurements, docs, screenshots, Debug/Release, graphify update

## Notes

- All never materializes 300k records.
- The ID query remains synchronous; measured 34–128 ms on 300k in Debug.
