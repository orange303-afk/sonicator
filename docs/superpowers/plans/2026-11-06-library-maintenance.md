# Library maintenance — implementation plan

Spec: `docs/superpowers/specs/2026-11-06-library-maintenance-design.md`

- [x] 1. `ScanMode::NewOnly` and `startNewOnly`; skip known paths and reconciliation; scan messages
- [x] 2. `SampleRepository::backupTo` through the SQLite backup API with destination guards
- [x] 3. Confirmation dialogs for Rescan and Reanalyze; `New files` button; sidebar layout
- [x] 4. Backup action in the footer and backup path/status reporting
- [x] 5. Tests: additive scan, cancellation keeps the previous row, backup consistency, dialogs, button wiring
- [x] 6. Docs, Debug/Release suites, CTest, graphify update

## Notes

- No analysis-version bump: feature output is unchanged.
- User backups are timestamped and never overwritten or deleted automatically.
