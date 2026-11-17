# Round tag menu — acceptance

Spec: `docs/superpowers/specs/2026-11-18-round-tag-menu-design.md`
Plan: `docs/superpowers/plans/2026-11-18-round-tag-menu.md`

The automated part is complete when `SonicatorTests` is green, including
`TestTagRanking.cpp` and `TestRoundTagLayout.cpp`. The checks below need a person at the screen;
tick each one with its result.

## Setting

- [ ] Settings → General → TAGS shows **Round tag menu**, off by default. The section stays visible
      in the VST3/AU editor.
- [ ] Turning it off closes an open ring and returns the row menu to five recent tags plus `Tag ▸`.

## Entry points

- [ ] With the setting on, right-clicking a row shows one **Tags** pill at the top instead of the
      recent rows and the `Tag` submenu; the rest of the menu is unchanged.
- [ ] Hovering the pill for about 200 ms closes the menu and opens the ring from the pill.
- [ ] Clicking the pill does the same when the pointer does not dwell.
- [ ] Ctrl/Cmd+T opens the ring for the selected rows; it is unavailable with an empty selection.
- [ ] With the folder tree focused, Ctrl/Cmd+T opens the ring for the selected folder.
- [ ] `Edit tags...` in the preview opens the ring for that one file.

## The ring

- [ ] The inner orbit holds the eight highest-ranked tags; the outer holds the next seven and
      `All tags…`; both are evenly spaced.
- [ ] A tag every selected file already has shows `✓`; picking it removes the tag and the status
      line names the removal.
- [ ] Picking an unassigned tag adds it to the whole selection and the status line reports the
      added count.
- [ ] `All tags…` opens the full list with ticks and `New tag...`; assigning from it works.
- [ ] Appearance takes about 260 ms; pills grow as the cursor approaches, neighbours grow less, and
      nothing jumps.
- [ ] Esc, a click outside the pills, a right click and a window resize dismiss the ring.
- [ ] With no tags at all, the ring still offers `All tags…` (which shows `No tags yet` and
      `New tag...`).

## Ranking

- [ ] In the live catalog the inner ring leads with tags assigned recently and often, not an
      alphabetical list.
- [ ] After assigning a tag a few times it moves inward on the next opening.
- [ ] A tag never assigned sits outside and, among such tags, alphabetically last.

## Folder

- [ ] `Tag folder…` opens the ring for that folder; the centre shows the folder name and the
      catalogued count.
- [ ] A pick shows the usual confirmation (path, subfolders, counts) and then the background
      batches; repeating it adds nothing twice.
- [ ] `All tags…` in the folder ring opens the full list and uses the same confirmation.

## Geometry and scale

- [ ] Near the window edge the ring shifts inward; no pill is clipped.
- [ ] In the plug-in editor at its minimum 880×580 the whole ring fits.
- [ ] At 125% and 150% interface scale the labels remain readable and nothing overlaps.

## Catalog

- [ ] Opening an existing schema-19 catalog writes a backup, migrates to 20 and seeds the order
      from recent tags.
- [ ] A plug-in build older than this one reports the catalog as newer and does not modify it, as
      before.

## Deferred / known limits

- Type-to-filter inside the ring, keyboard navigation between pills and several tags per opening
  are out of scope for v1.
- The ranking constants (30-day half-life, `+0.5`) are tuning values, not user settings.
