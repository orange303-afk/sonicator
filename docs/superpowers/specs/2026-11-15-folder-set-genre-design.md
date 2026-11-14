# Set genre for folders

Approved in conversation on 2026-11-15.

## Intent and scope

Add **Set genre...** to the folder context menu, with the existing canonical genre list and a
custom text option. This is a one-time assignment to catalogued files in the folder and all its
descendants, independent of table search, tags, stars, duplicate filters and pagination. Future
files do not inherit it. The virtual All samples node is not a folder target.

## Approaches

Considered extending automatic ancestor-name matching, a persistent folder-inheritance rule,
and a one-time manual assignment. The owner chose the last option to avoid changing inference
and inheritance behavior.

## Behavior and safety

- Store ordinary manual Genre overrides; analysis/rescanning must preserve them.
- Use existing genre names and allow trimmed, nonempty custom text.
- Before replacing a different manual genre (including Unknown or not applicable), ask for
  confirmation. Cancel changes nothing, including files without an override. The same manual
  genre does not require a replacement warning.
- Only catalog data changes: original audio, tags, stars, Type, BPM and Key are untouched.
- Run catalog work off the message thread and commit the entire assignment atomically.
- No schema change, new dependencies, automatic genre behavior or persistent folder rules.

Builder calls: follow folder-tagging ignore rules (ignored paths are excluded), but include
both members of hidden mono stereo pairs because this action targets every file in the folder.
Recheck the current scope and conflicts inside the write transaction. Show actual affected
counts and errors; never apply a deferred confirmation to a different catalog.
