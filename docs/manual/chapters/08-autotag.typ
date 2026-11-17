#import "../style.typ": *

#chapter(8, "AutoTag",
  lead: [AutoTag proposes sound-type tags from file names and stored metadata, then waits for you to review every proposal. It is a rule engine, not a classifier.],
  inthis: (
    [What AutoTag may propose],
    [The evidence it accepts and the evidence it refuses],
    [The review dialog and applying a selection],
    [Why some files are skipped, and the batch limits],
  ),
)[

#section("What it proposes")

AutoTag proposes only sound and instrument types that already exist as tags in the catalog. It reads
whole filename tokens and stored metadata; it never listens to the audio and never downloads
anything.

#data-table(
  header: ("Class", "Exact filename tokens (examples)"),
  widths: (1fr, 3.2fr),
  rows: (
    ("Kick", "`kick`, `kicks`, `kickdrum`, `kik`, `bd`, `bass drum`, `kick drum`, `808 kick`"),
    ("Snare", "`snare`, `snares`, `snr`, `sd`, `snare drum`"),
    ("Clap", "`clap`, `claps`, `handclap`"),
    ("Hat", "`hat`, `hats`, `hihat`, `hi-hat`, `hh`"),
    ("Percussion", "`perc`, `conga`, `bongo`, `djembe`, `tabla`, `cowbell`, `woodblock`, `cymbal(s)`"),
    ("Crash / Ride / Tom / Rim", "`crash(es)`, `ride(s)`, `tom(s)`, `rim`, `rimshot(s)`, `sidestick`"),
    ("Shaker / Tamb / Snap / Fill", "`shaker(s)`, `tamb`, `tambourine(s)`, `snap(s)`, `fingersnap`, `fill(s)`"),
    ("Riser / Upfilter / Downfilter", "`riser(s)`, `upfilter`, `uplifter`, `downfilter`, `downlifter`"),
    ("Bass / Sub", "`bass`, `bassline`, `808 bass`; `sub`, `subbass`, `sub bass`"),
    ("Lead / Pad / Keys", "`lead(s)`, `arp`, `pluck(s)`, `stab`, `riff`; `pad(s)`, `atmos`, `drone`; `keys`, `piano`, `organ`, `rhodes`"),
    ("Guitar / Vocal / FX", "`guitar`, `gtr`; `vocal(s)`, `vox`, `choir`, `acapella`; `fx`, `noise`, `sweep`, `impact`, `foley`"),
  ),
)

Tokens are matched whole and case-insensitively. `bd` inside another word, `kickstand`, or a note
number does not match. Plurals count for tokens longer than three characters. Compound rules win:
*bass drum* is Kick, not Bass plus Kick; *sub bass* is Sub; a bare *808* is ambiguous and is skipped.

Class types with no existing catalog tag — synth, chord, brass, strings — are skipped with an
explanation rather than inventing a tag.

#section("Evidence")

AutoTag accepts:

- an exact filename token from the table;
- a valid stored Type candidate with a known source (WAV/AIFF metadata, ASD, RX2 and so on);
- a completed smart Type result whose evidence is not merely the folder name.

Stored filename-derived smart candidates are ignored because AutoTag parses the same name with its
own instrument table. A folder or pack name is context only: it never establishes a type and cannot
veto a type stated by the filename or metadata. Two rows from one source are not two confirmations;
the engine looks for one unambiguous class, not a vote.

A manual Type override is never modified. A proposal that differs from it is listed but excluded
from application by default. Existing tags are never removed.

Every proposal carries the rule and a readable reason, for example `filename token "snare"` or
`stored metadata type "kick" from wav_smpl`. There are no confidence percentages.

#shot("/docs/manual/assets/autotag.png", [Figure 8.1 — The review dialog. Every proposal shows the file, the proposed tag, the reason and its state; the folder column is omitted here for width, but the real dialog shows it.], )

#section("Reviewing and applying")

Preparation runs off the UI thread and writes nothing. The dialog shows the scope, how many files
were checked, how many proposals are new, how many were already assigned, skip reasons, files with no
evidence and errors. Proposals can be excluded one by one; rows that are already assigned cannot be
applied again.

*Cancel* closes without writing. *Apply selected* writes only included, still-valid proposals. Before
each batch it revalidates that the sample and the tag still exist and that the file's size and
modification time match the snapshot. Changed or missing files are rejected and counted; a completed
batch stays committed if a later batch fails.

#section("Limits")

- AutoTag never proposes Loop, one-shot, BPM, key, mood, genre or structure.
- It is not machine learning: the rules are fixed and visible in the reason column.
- A scope over the budget stops with an explicit message instead of truncating: at most 1 000 000
  ids, 50 000 proposals and 500 assignments per transaction.
- Applied reasons are not stored; they are shown before applying.
- AutoTag waits for a running scan or smart analysis to finish. A folder *Tag* may run alongside
  preview.
- If a built-in instrument tag was renamed or deleted, AutoTag reports `tag-missing` and does not
  recreate it.

]
