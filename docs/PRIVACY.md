# Privacy

Sonicator works on your computer. There is no telemetry, no account and no hidden reporting. Two
things can leave the machine, and both are visible to you before they go.

## Reports you send yourself

Settings → Report opens a prefilled issue in your browser. You read it, edit it and press Submit
yourself; the application never posts it. The body holds the version, the OS, the last 60 lines of
the log and, if the previous session crashed, its backtrace. Your home folder becomes `~`, the
catalog path becomes `<catalog>` and your account name becomes `<user>`. File names inside the
catalog stay, because a report without the failing file is usually useless. Details:
`docs/DIAGNOSTICS.md`.

## Training data: off by default

Settings → Library has "Contribute anonymous feedback to improve classification models". It is off
until you answer the question shown once at startup, and it sends nothing while it is off. Turning
it on sends confirmed examples so the classifier and later models can learn from real libraries.

What one example contains:

| Sent | Not sent, ever |
|---|---|
| 14 acoustic measurements of the sample, rounded to 0.001 | the audio, or any part of it |
| the confirmed type and tags, and whether each is one of ours or yours | the file path, file name, folder name or pack name |
| key and tempo, when known | the file hash (it would reveal which files you own) |
| a random record id, the app version and `mac`/`win` | your name, computer name, e-mail or address |
| a random installation id, created when you opt in | your IP address (it is used for rate limiting and never stored) |

Confirmations you made before opting in are included, and the consent question says so. A custom
tag is text you typed, so it is the one field that could carry something personal by accident: tags
that look like a path, an address or a URL are dropped, and tags are limited to 32 characters.

A batched request is one `POST` of at most 256 KB, made on a background thread about once a day,
to `https://contribute.sampledicer.pro` (the Worker code is in
`services/contribute-worker/`). No request is made while indexing or playback is in trouble; a
failed request changes nothing and is retried the next day.

## What you can check and undo

- **What will be sent** in Settings shows the exact JSON of the next request.
- **Delete my contributed data** removes every batch stored under your installation id and issues a
  new id, so nothing can be linked to the old one. It also turns contribution off.
- The installation id can only be replaced by that deletion, so data never stays behind without a
  way to remove it.

Deletion reaches the stored batches immediately. A dataset or model that was already built and
released is rebuilt from the bucket each time, so the deleted data leaves the next release. A copy
somebody already downloaded cannot be recalled.

## Residual risk, stated plainly

The measurements are coarse but they are measurements: someone who has the vectors of a
commercial sample pack could match them and learn that an anonymous installation owns a sample from
that pack. No installation id is connected to a person, an address or a computer, and audio is
never sent, so the app accepts this risk. It is the only way to learn from real libraries without
collecting the libraries themselves.
