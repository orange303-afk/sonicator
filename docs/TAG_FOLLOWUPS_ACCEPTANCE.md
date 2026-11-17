# Tag follow-ups — acceptance

Three additions on top of the round tag menu stage.

## Assign last used tag

- [ ] Settings → Hotkeys → Library lists **Assign last used tag** with ⇧⌘T and accepts a different
      key.
- [ ] With two or more files selected, the key applies the last tag assigned in this catalog.
- [ ] With nothing selected, or before any tag has been assigned, the command is unavailable and
      the key does nothing.

## AutoTag preview

- [ ] Open AutoTag for a selection; clicking a proposal row plays that file through the preview
      panel.
- [ ] Moving through rows with the arrow keys auditions each file once, not once per repaint.
- [ ] Closing the dialog, Cancel or Apply stops playback.
- [ ] A missing file is skipped without an error dialog.

## Type first in AutoTag

- [ ] A file whose Type is a manual override gets that type's tag, even when the filename says a
      different instrument.
- [ ] A file with a resolved Type from metadata or acoustic analysis gets that tag before a filename
      token.
- [ ] A file whose Type came from its own filename still gets the specific filename tag (for
      example Tom, not Percussion).
- [ ] A manual Type with no AutoTag category (*Synth*) is skipped with an explanation, not tagged
      from the filename.
- [ ] Marking the Type unknown still skips the type proposal.
- [ ] Existing tags are never removed by any of these paths.

## AutoTag review editor

- [ ] Several proposed tags for one file share one row and appear as pills in **Tags**.
- [ ] Alt-click a pill dims it and drops it from Apply; Alt-click again restores it.
- [ ] The **Use** box includes or excludes every tag of that file.
- [ ] Clicking a pill or the `+` opens the round tag menu anchored there; picking a tag adds a
      proposal, picking a checked tag drops it; `All tags…` lists everything.
- [ ] Added proposals are applied only by **Apply selected**, and Apply writes nothing before it.
- [ ] Selecting a row still auditions the file.
- [ ] Rows whose proposals are all already assigned cannot be applied.

## Automated coverage

`TestAutoTag.cpp` pins the new order and the manual override cases. The preview and the shortcut are
UI behaviour covered by this manual list.
