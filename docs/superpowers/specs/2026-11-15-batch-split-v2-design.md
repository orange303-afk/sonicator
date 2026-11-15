# Batch v2 — Split as a cardinality-changing step

Date: 2026-11-15. Status: design for approval. Direction chosen by the owner:
"Batch v2: Split-шаг + полировка".

## Intent

A batch job can split each source at silence and then process every resulting part through the rest
of the chain, producing one output per part and format. This turns Batch Processing from a
converter into a preparation tool for loops and one-shots without leaving Sonicator.

## Decisions

- **One Split step per recipe.** It may appear anywhere in the chain: steps before it run on the
  whole source, steps after it run on each part. A second Split is rejected by validation (the part
  tree would otherwise be combinatorial).
- **Cardinality is discovered during the job.** Part detection is a full decode pass, so preflight
  cannot promise a part count. The dialog shows "each part x formats"; the report shows the actual
  parts. Start is not blocked by an unknown count, but the worker reports Analyzing progress per
  source.
- **Naming.** A source that yields one part keeps `<stem> processed.<ext>`. Two or more parts use
  `<stem> part 001.<ext>`, `part 002`, ... with a three-digit minimum, widened when the count
  exceeds 999. Path reservations are shared across parts and formats, so parts never overwrite each
  other.
- **Failure isolation.** A part that fails to prepare, encode, validate or publish is reported
  individually; the remaining parts and formats continue. A source whose split detection fails
  produces `decode_failed` and no parts.
- **Chain semantics.** Trim/Normalize/DC/Fade after Split see that part's own boundaries and
  statistics, not the parent file's. A Trim before Split narrows the range that is split.
- **Preview.** Preview uses the first sound part after the chain, at most 10 seconds, through the
  existing dry/wet path. It says "first part" in the status line.
- **Report.** Each output carries the parent sample id, part index, part count and the actual path;
  counts distinguish sources, parts and outputs. Cancellation between parts keeps completed parts
  and lists unstarted ones as cancelled.

## Split parameters

Threshold -120..-6 dBFS (default -60), minimum silence 10..10000 ms (default 250), minimum part
10..10000 ms (default 100). They reuse `SplitSilenceOptions` semantics from SplitService. Padding is
not added in v2.

## Polish in the same stage

- Acceptance screenshots at 100%/125%, light/dark, on real files.
- ENOSPC and a >2^31-frame counter test through test seams, not product-only switches.
- A deterministic in-flight barrier test for close/catalog switch.
- OGG non-ASCII comment display: verify whether the app surfaces JUCE's Latin-1 reader view; fix
  only if a real UI path exposes it.
- Windows CI build if the existing workflow can run it; otherwise keep the honest macOS-only note.

## Non-goals

Original overwrite, part-level presets, nested splits, loop/cue remapping, Split in the preview
editor (the existing Split by silence command keeps working separately).

## Open builder questions

None that change the user-visible behaviour; the exact part numbering width and the analysis pass
cost are implementation details to cover with tests.
