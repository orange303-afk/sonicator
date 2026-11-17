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

## Automated coverage

`TestAutoTag.cpp` pins the new order and the manual override cases. The preview and the shortcut are
UI behaviour covered by this manual list.
