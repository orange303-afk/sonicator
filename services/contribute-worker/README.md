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

The public address is `https://contribute.sampledicer.pro`, served by a Worker route on that zone
with a proxied CNAME. `*.workers.dev` also works but is blocked by HTTP/2 on some networks (including
the development machine), so the app uses the custom domain.

**Deploy this Worker whenever the intake schema changes.** A released app sends the newest
`featureSchema` it knows, and the intake rejects a batch as a whole when any record fails validation,
so a stale Worker silently stops all contribution: the app ignores a non-2xx response, keeps its
watermark and retries at the next launch. Production ran a 14-feature-only Worker from 2026-11-11 to
2026-11-21, which rejected every 84-feature record with `400 invalid record` while `/v1/health`
still answered `ok`.

After a deploy, check both schemas against the live endpoint (it writes one test object and removes
it again):

```sh
node --input-type=module -e '
const installId = crypto.randomUUID();
const post = (features, featureSchema) => fetch("https://contribute.sampledicer.pro/v1/contribute", {
  method: "POST", headers: { "content-type": "application/json" },
  body: JSON.stringify({ schema: "contribute.v1", installId, batchId: crypto.randomUUID(),
    appVersion: "0.0.0-diagnostic", os: "mac", sentAt: new Date().toISOString(),
    records: [{ recordId: crypto.randomUUID(), featureSchema, features, tags: [] }] }),
});
for (const [count, schema] of [[14, 1], [84, 2]]) {
  const response = await post(Array.from({ length: count }, () => 0.5), schema);
  console.log(`schema ${schema}:`, response.status, (await response.text()).trim());
}'
```

`204` for both is a working intake; remove the test objects under `raw/<installId>/` in the bucket
afterwards.

The first deploy needs the bucket to exist and the route to be enabled for the script:

```sh
curl -X PUT "https://api.cloudflare.com/client/v4/accounts/$ACCOUNT/r2/buckets/sonicator-contribute" ...
curl -X POST "https://api.cloudflare.com/client/v4/zones/$ZONE/dns_records" \
  -H "authorization: Bearer $TOKEN" -H "content-type: application/json" \
  --data '{"type":"CNAME","name":"contribute","content":"sonicator-contribute.orange303.workers.dev","proxied":true}'
curl -X POST "https://api.cloudflare.com/client/v4/zones/$ZONE/workers/routes" \
  -H "authorization: Bearer $TOKEN" -H "content-type: application/json" \
  --data '{"pattern":"contribute.sampledicer.pro/*","script":"sonicator-contribute"}'
```

## Export for training

```sh
CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=... node scripts/export.mjs --out contribute.jsonl
```

The script drops deleted install ids, keeps the last received version of each `recordId`, writes
JSONL and prints the most frequent custom tags and types. The dataset and the model prototypes are
always rebuilt from R2 this way, so a deletion lands in the next release.

## Scheduled local copy

`tools/contribution-sync.mjs` does the same export into the project and is what the training stage
reads: `data/contributions/latest/contribute.jsonl` plus `report.md`, and one row per run in
`data/contributions/history.csv`. That directory is git-ignored and never committed. Credentials come
from the login keychain (service `Sonicator.Cloudflare.R2`, account `sonicator-contribute`), so an
unattended run needs no environment. Run it by hand with `node tools/contribution-sync.mjs`.

On macOS a LaunchAgent runs it every six hours:
`~/Library/LaunchAgents/pro.sampledicer.sonicator.contribution.plist`, logging to
`data/contributions/sync.log`. Remove it with
`launchctl bootout gui/$(id -u)/pro.sampledicer.sonicator.contribution`.
