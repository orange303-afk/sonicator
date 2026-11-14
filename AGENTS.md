# AI Assistant & Agent Guidelines for Sonicator

## Feature Backlog Source of Truth
The external roadmap and feature backlog for Sonicator is maintained at:
`~/Documents/orange303/Sonicator Features.md`

**Rule:**
Always consult `~/Documents/orange303/Sonicator Features.md` when:
- Planning new milestones or architectural stages.
- Proposing roadmap features or discussing backlog priorities.
- Reviewing feature requests or integration ideas (e.g., Sample Dicer / Loop Dicer integration, crash reporting, tag cloud, VST3/AU DAW plugins, AI agents, etc.).

## Knowledge Graph & Architecture
- Graphify knowledge graph is stored in `graphify-out/`.
- After modifying code, run `graphify update .` to keep the graph current.
- Architecture details are documented in `docs/ARCHITECTURE.md`.

## Feature Specifications
- Batch Processing and audio converter plan and requirements: `docs/superpowers/specs/2026-11-14-batch-audio-converter-design.md` (design only; not implemented).
- User plugin system plan and requirements: `docs/superpowers/specs/2026-11-14-user-plugins-design.md` (design only; not implemented).
- Genre column: implemented (schema 17); see `docs/GENRE.md`, spec `docs/superpowers/specs/2026-11-14-genre-column-design.md`.
- Customizable keyboard shortcuts: implemented in `stage-settings-hotkeys-layout`; see `docs/COMMANDS.md`, `docs/SHORTCUTS.md`, spec `docs/superpowers/specs/2026-11-14-customizable-shortcuts-design.md`.
- Settings navigation: implemented in `stage-settings-hotkeys-layout`; see `docs/SETTINGS_HOTKEYS_LAYOUT_ACCEPTANCE.md`, spec `docs/superpowers/specs/2026-11-14-settings-navigation-design.md`.
- Workspace Studio styling (header, toolbar, table, Analysis, Preview, dialogs): implemented in `stage-workspace-studio`; see `docs/WORKSPACE_STUDIO_ACCEPTANCE.md`, spec `docs/superpowers/specs/2026-11-14-workspace-studio-design.md`, plan `docs/superpowers/plans/2026-11-14-workspace-studio.md`.
