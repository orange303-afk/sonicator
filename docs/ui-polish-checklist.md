# Library UI polish

- [x] Consolidate library actions into LIBRARY menu; add search clear control.
- [x] Move container source selection into a vertical Sample Analysis list.
- [x] Add image nodes and virtual folders to folder tree, with recursive file listings.
- [x] Refine Settings close control and error colours.
- [x] Run tests, refresh graph, build Release, launch application.

## Constraints

MDX is recognised and shown in the tree, but this build has no MDX filesystem decoder. Opening it reports the existing unsupported-format error instead of pretending to expand it.

## Verification

- Focused UI tests cover the menu, clear search action, source-list placement and selection, virtual image directories, recursive listing and preview decoding.
- Full CTest suite passes.
- Release archive: `build-release/SONICATOR-macOS.zip`.
- A clean ad-hoc-signed copy is used for launching and packaging, because macOS FileProvider reattaches FinderInfo to app bundles inside Documents and invalidates strict bundle verification there.
- Review follow-up: leaving an image invalidates pending decode work; failed/modified images reopen; FM/error tracker slots use red text. Regression tests cover each case, and the follow-up review found no remaining blocker in these fixes.
- Akai follow-up: filesystem root ID zero is valid. Browser readiness is tracked explicitly rather than inferred from the root ID; a synthetic Akai regression covers publishing the tree, recursive files, cached opens and directory navigation. An opt-in real-image UI acceptance test also selects the image through the folder tree and decodes a descendant sample.
- Image filtering/transport follow-up: the library checkbox also filters image entries with unsupported/error capabilities or zero declared size. Decode failures update the filtered list immediately; toggling off restores the rows. Changing selection or opening/closing an image stops the old transport immediately and invalidates pending decode work. Selection restoration during filtering does not retrigger playback. Multi-track CUE choices stay separate from sample decoding.
- Tracker/menu layout follow-up: source rows sit below analysis data and SMART METADATA. Menus explicitly use the application look-and-feel, and context menus target the clicked screen point with their owning component and a downward preference (screen-edge fitting still applies). Original MOD/S3M text and XM instrument text preserve spacing and display in compact monospaced rows without technical suffixes. XM text-only instruments, including standard 29-byte headers, remain visible as blank/audio-less rows rather than disappearing.
