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

## File Safety: exported audio is project media
`~/Library/Sonicator/exports/` — including `exports/drag/` — holds audio that DAW projects may
reference: selection exports and the WAVs materialised for drag-and-drop (EMU `.ebl`, REX2, REX
selections). These files are **not** test leftovers.

**Rule:**
- Never delete, move or "clean up" the exports folder or the drag cache without the owner's
  explicit request.
- Before removing any exported audio, check the DAW projects (Cubase `.cpr`, REAPER `.rpp`,
  Live `.als`, Bitwig `.bwproject`) for references to it.
- A cleanup of temporary files must name its targets explicitly, not a whole directory.

## Feature Specifications
- Batch Processing v1 and audio converter: implemented; see `docs/BATCH_PROCESSING.md`, acceptance `docs/BATCH_PROCESSING_ACCEPTANCE.md`, spec `docs/superpowers/specs/2026-11-15-batch-processing-tz.md`. The broader converter design `docs/superpowers/specs/2026-11-14-batch-audio-converter-design.md` covers later stages.
- User plugin system plan and requirements: `docs/superpowers/specs/2026-11-14-user-plugins-design.md` (design only; not implemented).
- Genre column: implemented (schema 17); see `docs/GENRE.md`, spec `docs/superpowers/specs/2026-11-14-genre-column-design.md`.
- Customizable keyboard shortcuts: implemented in `stage-settings-hotkeys-layout`; see `docs/COMMANDS.md`, `docs/SHORTCUTS.md`, spec `docs/superpowers/specs/2026-11-14-customizable-shortcuts-design.md`.
- Settings navigation: implemented in `stage-settings-hotkeys-layout`; see `docs/SETTINGS_HOTKEYS_LAYOUT_ACCEPTANCE.md`, spec `docs/superpowers/specs/2026-11-14-settings-navigation-design.md`.
- Workspace Studio styling (header, toolbar, table, Analysis, Preview, dialogs): implemented in `stage-workspace-studio`; see `docs/WORKSPACE_STUDIO_ACCEPTANCE.md`, spec `docs/superpowers/specs/2026-11-14-workspace-studio-design.md`, plan `docs/superpowers/plans/2026-11-14-workspace-studio.md`.
- DAW plugin P1: `SONICATOR` VST3/AU browser on the shared catalog with project state and disabled heavy jobs; see `docs/PLUGIN.md`, plan `docs/superpowers/plans/2026-11-15-plugin-product-p1.md`.
- Plugin Match BPM / Match Scale (P2): implemented in `stage-plugin-match` with offline Rubber Band 3.3.0 rendering, per-project toggles, prepared preview/waveform and shared matched drag. REX2/sampler/tracker/disc matching is refused, not guessed; manual DAW/auval acceptance and Windows/universal builds remain open. See spec `docs/superpowers/specs/2026-11-16-plugin-match-bpm-scale-design.md`, plan `docs/superpowers/plans/2026-11-16-plugin-match-bpm-scale.md`, acceptance `docs/PLUGIN_MATCH_ACCEPTANCE.md`.
- Catalog safety and plugin state: the plugin never migrates the shared catalog (schema 19 in `core/database/CatalogSchema.h`, read-only check in `core/database/CatalogCompatibility.h`); embedded dialogs and state restore while the editor is open live in `ui/shell/ThemedDialogs.h` and `plugin/PluginEditor.cpp`; see `docs/PLUGIN.md`, plan `docs/superpowers/plans/2026-11-15-plugin-safety-and-beta.md`.
- Cloud tag map: the library has a second view, "Cloud". One node per tagged file; tag islands are packed touching into one continuous cloud, real timbre descriptors place files inside them, and 84-value vectors sharpen the local neighbourhood where they exist. No island discs are drawn: a soft colour field under the points carries the gradients, ambient sway/breathing is display-only, and a feathered hover lens magnifies the neighbourhood under the cursor. Spec `docs/superpowers/specs/2026-11-15-cloud-tag-map-design.md`, plan `docs/superpowers/plans/2026-11-15-cloud-tag-map.md`, notes and measurements `docs/CLOUD_MAP.md`. The layout is deterministic and cached by fingerprint (`core/cloud/`); the view draws world-space layers and never rebuilds on a filter change (`ui/cloud/`); hover audio is a separate voice in `PreviewOutput` (`core/audio/HoverAudition`). Untagged files are not on the map, and files without a decodable format stay silent with an honest caption.
