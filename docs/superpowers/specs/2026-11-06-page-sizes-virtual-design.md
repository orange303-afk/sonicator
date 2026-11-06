# Page sizes and virtual all view — design

Stage after hide duplicates. Scope: choose 100 / 1 000 / 10 000 / All rows next to the pagination
arrows and keep the All view responsive on a 300k catalog.

## Goals

- Page-size menu to the right of `<` / `>`; the choice persists.
- All shows every match without building a 300k-record vector or blocking scrolling.
- Selection, tags, stars, duplicate counts, sorting and filters keep working in All.
- Switching pages remains SQL-driven; no client-side sorting of a page.

## Decisions

- Finite sizes keep `query(limit, offset)`.
- All uses `queryIds`: one SQL statement returning only matching primary keys in catalog order.
  The table row count is that vector (300k × 8 bytes, a few MB). Each paint outside the cache
  requests a 400-row block; `recordsByIds` plus `tagsForSamples` and `duplicateCounts` fill it.
  A 4000-row window around the viewport is cached; older rows are evicted with their metadata.
- `TableListBox` already virtualizes row painting, so no widgets are created for off-screen rows.
- Selection is stored by sample ID. A reload restores it and scrolls to the selected row; without
  a selection the list returns to the top.
- Page size is a display preference in `settings.json`, default 10 000, invalid values fall back.
- Intermediate refreshes during indexing are skipped in All mode; the list reloads once at the
  end, avoiding repeated ID queries and scroll jumps.
- Measured Debug: unfiltered ID query 34 ms for 300k by name, 128 ms with a text filter, 400-row
  block 2 ms. The only remaining synchronous cost is the ID query when filters/sort change.

## Non-goals

- No background thread for the ID query yet.
- No cross-page selection; selection remains within the current result.
- No full-record materialization for All.

## Verification

Repository ID/order tests; virtual block loading and selection; shell switching 100→All→100,
on-demand row load, next page, persisted choice; 300k scale timings; snapshots of the All view.
