# Sonicator contribution intake

A Cloudflare Worker that receives anonymous feature-and-label batches from the Sonicator desktop
app and stores them in R2. Design and privacy rules:
`docs/superpowers/specs/2026-11-11-training-data-contribution-design.md`, `docs/PRIVACY.md`.

| Route | Purpose |
|---|---|
| `GET /v1/health` | `200 ok` after a deploy |
| `POST /v1/contribute` | Validates a batch and writes `raw/<installId>/<batchId>.json`; `204` |
| `DELETE /v1/contributions/<installId>` | Deletes every batch of that install id and leaves `deleted/<installId>`; `204` |

## Develop and test

```sh
npm install
npx vitest run     # runs inside the Workers runtime with a local R2 bucket
npx tsc --noEmit
```

## Deploy

There is no secret in the Worker: the bucket is reached through the `CONTRIBUTE` R2 binding.

```sh
CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=... npx wrangler deploy
```

The first deploy needs the bucket to exist and the `workers.dev` route to be enabled for the
script:

```sh
curl -X PUT "https://api.cloudflare.com/client/v4/accounts/$ACCOUNT/r2/buckets/sonicator-contribute" ...
curl -X POST "https://api.cloudflare.com/client/v4/accounts/$ACCOUNT/workers/scripts/sonicator-contribute/subdomain" \
  -H "authorization: Bearer $TOKEN" -H "content-type: application/json" --data '{"enabled":true}'
```

## Export for training

```sh
CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=... node scripts/export.mjs --out contribute.jsonl
```

The script drops deleted install ids, keeps the last received version of each `recordId`, writes
JSONL and prints the most frequent custom tags and types. The dataset and the model prototypes are
always rebuilt from R2 this way, so a deletion lands in the next release.
