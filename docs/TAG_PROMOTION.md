# Standard tag and type promotion

Custom labels typed by many people are the backlog's item 13. Nothing is promoted automatically:
a new standard tag or type changes what every catalog shows, so the owner reads the candidates and
records each decision here.

## How a candidate is produced

1. Export the bucket: `node scripts/export.mjs --out out/contribute-export.jsonl` in
   `services/contribute-worker/`. Only rows confirmed by a person are exported.
2. Build the report: `node candidates.mjs --in ../contribute-worker/out/contribute-export.jsonl`
   in `services/training/`. It writes `docs/tag-candidates.md` and
   `services/training/out/candidates.json`.
3. The report keeps a label when at least five installs and at least 1% of installs confirmed it,
   and drops paths, addresses, URLs, numbers and fragments shorter than two characters. A match
   with a standard name, ignoring case, is reported as a merge rather than a new label.

## How a promotion is applied

A promotion is its own commit, after the owner has approved the specific names in the report.

- A tag goes into `standardTags` in `core/database/SampleRepository.cpp` and into a new
  `schema14Tags` block (the upgrade inserts only that block, so a tag the user deleted is not
  recreated). The catalog schema version is bumped; every `PRAGMA user_version` expectation in the
  tests moves with it.
- A type goes into the `typeAliases` table in `core/smart/NameHeuristics.cpp`, which is also the
  list `canonicalSmartTypeNames()` returns and the contribution vocabulary uses.
- User data is not rewritten. If a person already has the tag under the same name, it stays their
  tag with their colour; `custom_types` and existing type values are untouched. The standard label
  simply exists beside them.
- `services/training/test/candidates.node.mjs` reads both source files and fails when the lists in
  `standard.mjs` drift from the application, so the next report matches what ships.

Custom types are now always reported as custom (`typeIsCustom`): the contribution vocabulary holds
only the canonical names that ship, never the catalog's `custom_types`. Before this, a user's own
type was labelled standard in the dataset.

## Decisions

| Date | Label | Kind | Installs | Decision | Schema |
|---|---|---|---|---|---|
| 2026-11-11 | — | — | — | No candidates: the bucket holds no records yet | 14 |

Add one row per promoted or rejected label. Rejections are worth recording too: they stop the same
name from being reconsidered every few months.
