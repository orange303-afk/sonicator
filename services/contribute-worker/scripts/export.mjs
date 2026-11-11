#!/usr/bin/env node
// Exports the intake bucket as one JSONL file for the training stage.
//
// Reads through the Cloudflare REST API, so it needs an account-level token with
// Workers R2 Storage:Read. The token and the account id stay in the environment:
//   CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=... node scripts/export.mjs --out data.jsonl
//
// Rules from the design:
// - an install id with a tombstone contributes nothing;
// - for one recordId the record received last wins, so a later correction replaces the label.

import { writeFile } from "node:fs/promises";
import process from "node:process";

const api = "https://api.cloudflare.com/client/v4";
const accountId = process.env.CLOUDFLARE_ACCOUNT_ID;
const token = process.env.CLOUDFLARE_API_TOKEN;
const bucket = process.env.CONTRIBUTE_BUCKET ?? "sonicator-contribute";
const args = process.argv.slice(2);
const outIndex = args.indexOf("--out");
const out = outIndex >= 0 ? args[outIndex + 1] : "contribute-export.jsonl";

if (!accountId || !token) {
  console.error("CLOUDFLARE_ACCOUNT_ID and CLOUDFLARE_API_TOKEN are required");
  process.exit(2);
}

async function request(path) {
  const response = await fetch(`${api}/accounts/${accountId}/r2/buckets/${bucket}${path}`, {
    headers: { authorization: `Bearer ${token}` },
  });
  if (!response.ok) throw new Error(`${response.status} ${await response.text()}`);
  return response;
}

async function list(prefix) {
  const keys = [];
  let cursor;
  for (;;) {
    const query = new URLSearchParams({ prefix, per_page: "1000" });
    if (cursor) query.set("cursor", cursor);
    const page = await (await request(`/objects?${query}`)).json();
    for (const object of page.result ?? []) keys.push(object.key);
    if (!page.result_info?.is_truncated) return keys;
    cursor = page.result_info.cursor;
  }
}

const deleted = new Set((await list("deleted/")).map((key) => key.slice("deleted/".length)));
const latest = new Map();
let installs = new Set();
let received = 0;

for (const key of await list("raw/")) {
  const [, installId, name] = key.split("/");
  if (!installId || deleted.has(installId)) continue;
  const batch = await (await request(`/objects/${encodeURIComponent(key)}`)).json();
  if (batch.installId !== installId) continue;
  installs.add(installId);
  received += 1;
  for (const record of batch.records ?? []) {
    const previous = latest.get(record.recordId);
    if (previous === undefined || String(batch.sentAt) >= String(previous.sentAt))
      latest.set(record.recordId, { installId, sentAt: batch.sentAt, appVersion: batch.appVersion, ...record });
  }
}

const lines = [...latest.values()].map((record) => JSON.stringify(record));
await writeFile(out, lines.length > 0 ? `${lines.join("\n")}\n` : "");

const customTypes = new Map();
const customTags = new Map();
for (const record of latest.values()) {
  if (record.typeIsCustom) customTypes.set(record.type, (customTypes.get(record.type) ?? 0) + 1);
  for (const tag of record.tags ?? [])
    if (tag.custom) customTags.set(tag.name, (customTags.get(tag.name) ?? 0) + 1);
}

const top = (map) => [...map.entries()].sort((a, b) => b[1] - a[1]).slice(0, 20);

console.log(`wrote ${out}`);
console.log(`batches read: ${received}, installs: ${installs.size}, deleted installs: ${deleted.size}`);
console.log(`records: ${latest.size}`);
if (customTypes.size > 0) console.log("custom types:", top(customTypes).map(([name, count]) => `${name} (${count})`).join(", "));
if (customTags.size > 0) console.log("custom tags:", top(customTags).map(([name, count]) => `${name} (${count})`).join(", "));
