#import "../style.typ": *

#heading(level: 1)[Appendix B · Search syntax]

Everything combines with *and*. Prefixes and masks can sit next to plain words in one query.

#data-table(
  header: ("Write", "To find"),
  widths: (1.6fr, 3fr),
  rows: (
    ("`kick`", "Files whose name or path contains the token."),
    ("`kick 808`", "Both tokens, in any order."),
    ("`\"bass drum\"`", "The exact phrase, not the two words separately."),
    ("`\"rimshot\"`", "The exact word; `rim` inside `rimshot` does not match."),
    ("`*.rx2`", "Every file name ending in `.rx2`."),
    ("`kick_*`", "File names that start with `kick_`."),
    ("`ext:wav`", "WAV files."),
    ("`ext:rx2,sf2`", "REX2 or SoundFont 2 files."),
    ("`*.rex/rx2/sf2`", "The compact form of several masks."),
    ("`*.rx2|*.sf2`", "Either mask; alternation."),
    ("`type:kick`", "Effective type Kick, manual or automatic."),
    ("`key:Am`", "Effective key A minor."),
    ("`key:A`", "Any A key, without a mode."),
    ("`note:C4`", "A resolved sampler root note of C4."),
    ("`genre:house`", "Effective genre containing `house`."),
    ("`pad`", "Files named with `pad`, plus files whose type resolves to Pad."),
    ("`C4`", "Files named with C4, plus files whose resolved root is C4."),
  ),
)

#section("Details that save time")

- The field applies after a 250 ms pause; Enter applies immediately; Escape clears or closes history.
- Click the field to reopen the last ten applied searches; the list is shared by every catalog.
- Header sorting sorts the whole filtered result, not the page.
- *Clear filters* clears tags, stars and Hide empty; it does not clear the query.
- A value the application could not resolve does not match `type:`, `key:`, `note:` or `genre:`.
  Ambiguous is not silently turned into a guess.
