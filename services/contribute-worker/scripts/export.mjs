#!/usr/bin/env node
// Exports the intake bucket as one JSONL file for the training stage.
//
// Reads through the Cloudflare REST API, so it needs an account-level token with
// Workers R2 Storage:Read. The token and the account id stay in the environment:
//   CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=... node scripts/export.mjs --out data.jsonl
//
// Rules from the design:
// - an install id with a tombstone contributes nothing;
// - for one recordId the record received last wins, so a later correction replaces the label;
// - a record whose features are not the known shape stops the export instead of reaching training;
// - the report only lists labels confirmed by enough distinct installs, so one person cannot put
//   a label into it by repeating it.

import { mkdir, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import process from "node:process";
import { pathToFileURL } from "node:url";

export const FEATURE_SCHEMAS = new Map([[1, 14], [2, 84]]);
export const FEATURE_SCHEMA = 1;
export const FEATURE_COUNT = 14;
export const DEFAULT_MIN_INSTALLS = 3;

const api = "https://api.cloudflare.com/client/v4";

function isFiniteFeature(value) {
  return typeof value === "number" && Number.isFinite(value) && value >= 0 && value <= 1;
}

function installOf(record) {
  return record.installId;
}

// One record per recordId: the last batch received wins. Batch order comes from the sorted object
// keys, and a tie on sentAt is settled by batchId, so two runs over the same bucket agree.
export function latestRecords(batches) {
  const ordered = [...batches].sort((a, b) =>
    String(a.sentAt).localeCompare(String(b.sentAt)) || String(a.batchId).localeCompare(String(b.batchId)));
  const latest = new Map();
  for (const batch of ordered) {
    for (const record of batch.records ?? []) {
      const previous = latest.get(record.recordId);
      if (previous === undefined || String(batch.sentAt) >= String(previous.sentAt))
        latest.set(record.recordId, {
          installId: batch.installId,
          appVersion: batch.appVersion,
          sentAt: batch.sentAt,
          ...record,
        });
    }
  }
  return [...latest.values()];
}

export function assertValid(records) {
  for (const record of records) {
    const where = `record ${record.recordId} from install ${record.installId}`;
    if (typeof record.recordId !== "string" || record.recordId.length === 0)
      throw new Error("a record has no recordId");
    if (typeof record.installId !== "string" || record.installId.length === 0)
      throw new Error(`${where}: missing installId`);
    const expectedFeatures = FEATURE_SCHEMAS.get(record.featureSchema);
    if (expectedFeatures === undefined)
      throw new Error(`${where}: featureSchema ${record.featureSchema} is not 1 or 2`);
    if (!Array.isArray(record.features) || record.features.length !== expectedFeatures)
      throw new Error(`${where}: expected ${expectedFeatures} features, got ${record.features?.length}`);
    for (const value of record.features)
      if (!isFiniteFeature(value))
        throw new Error(`${where}: feature ${value} is outside 0..1`);
    if (record.type !== undefined && (typeof record.type !== "string" || record.type.trim().length === 0))
      throw new Error(`${where}: empty type`);
    if (record.tags !== undefined && !Array.isArray(record.tags))
      throw new Error(`${where}: tags is not an array`);
    for (const tag of record.tags ?? [])
      if (typeof tag?.name !== "string" || tag.name.trim().length === 0)
        throw new Error(`${where}: empty tag name`);
    if (record.type === undefined && (record.tags ?? []).length === 0)
      throw new Error(`${where}: no type and no tags`);
  }
}

function countLabels(records, labelsOf) {
  const labels = new Map();
  for (const record of records) {
    for (const { name, custom } of labelsOf(record)) {
      const entry = labels.get(name.toLowerCase()) ?? {
        label: name,
        support: 0,
        installs: new Set(),
        custom: false,
      };
      entry.support += 1;
      entry.installs.add(installOf(record));
      entry.custom = entry.custom || Boolean(custom);
      labels.set(name.toLowerCase(), entry);
    }
  }
  return [...labels.values()]
    .map((entry) => ({
      label: entry.label,
      support: entry.support,
      installSupport: entry.installs.size,
      custom: entry.custom,
    }))
    .sort((a, b) =>
      b.installSupport - a.installSupport || b.support - a.support || a.label.localeCompare(b.label));
}

export function summarize(records, minInstalls = DEFAULT_MIN_INSTALLS) {
  const installs = new Set(records.map(installOf));
  const types = countLabels(records, (record) =>
    record.type ? [{ name: record.type, custom: record.typeIsCustom }] : []);
  const tags = countLabels(records, (record) =>
    (record.tags ?? []).map((tag) => ({ name: tag.name, custom: tag.custom })));
  const enough = (label) => label.installSupport >= minInstalls;
  const customTypes = types.filter((type) => type.custom).reduce((sum, type) => sum + type.support, 0);
  const customTags = tags.filter((tag) => tag.custom).reduce((sum, tag) => sum + tag.support, 0);
  const share = (custom, all) => (all === 0 ? 0 : Math.round((custom / all) * 1000) / 1000);
  const average = (labels) =>
    labels.length === 0 ? 0 : Math.round((labels.reduce((sum, label) => sum + label.support, 0) / labels.length) * 10) / 10;
  return {
    featureSchemas: [...new Set(records.map((record) => record.featureSchema))].sort(),
    minInstalls,
    installs: installs.size,
    records: records.length,
    types: types.filter(enough),
    tags: tags.filter(enough),
    customTypeShare: share(customTypes, records.length),
    customTagShare: share(customTags, records.reduce((sum, record) => sum + (record.tags?.length ?? 0), 0)),
    averageSupportPerType: average(types),
    averageSupportPerTag: average(tags),
  };
}

function table(rows, header) {
  const lines = [`| ${header.join(" | ")} |`, `|${header.map(() => "---").join("|")}|`];
  for (const row of rows) lines.push(`| ${row.join(" | ")} |`);
  return lines.join("\n");
}

export function renderReport(summary, { generatedAt } = {}) {
  const labels = (list, name) =>
    list.length === 0
      ? `No ${name} reached ${summary.minInstalls} installs.`
      : table(list.slice(0, 50).map((label) =>
          [label.label, label.support, label.installSupport, label.custom ? "custom" : "standard"]),
        [name, "Records", "Installs", "Kind"]);
  return `# Contribution export

Generated: ${generatedAt ?? "unknown"}

- Feature schemas: ${(summary.featureSchemas ?? []).join(", ")}
- Installs: ${summary.installs}
- Records: ${summary.records}
- Minimum installs for a label in this report: ${summary.minInstalls}
- Custom type share of records: ${(summary.customTypeShare * 100).toFixed(1)}%
- Custom tag share of tag assignments: ${(summary.customTagShare * 100).toFixed(1)}%
- Average support per type: ${summary.averageSupportPerType}
- Average support per tag: ${summary.averageSupportPerTag}

## Types

${labels(summary.types, "Type")}

## Tags

${labels(summary.tags, "Tag")}
`;
}

export async function writeExport(records, out, { minInstalls = DEFAULT_MIN_INSTALLS, generatedAt } = {}) {
  assertValid(records);
  const summary = summarize(records, minInstalls);
  const lines = records.map((record) => JSON.stringify(record));
  await mkdir(dirname(out), { recursive: true });
  await writeFile(out, lines.length > 0 ? `${lines.join("\n")}\n` : "");
  const report = join(dirname(out), "report.md");
  await writeFile(report, renderReport(summary, { generatedAt: generatedAt ?? new Date().toISOString() }));
  return { summary, report };
}

// --- bucket access -------------------------------------------------------------------------

async function request(accountId, token, bucket, path) {
  const response = await fetch(`${api}/accounts/${accountId}/r2/buckets/${bucket}${path}`, {
    headers: { authorization: `Bearer ${token}` },
  });
  if (!response.ok) throw new Error(`${response.status} ${await response.text()}`);
  return response;
}

async function list(accountId, token, bucket, prefix) {
  const keys = [];
  let cursor;
  for (;;) {
    const query = new URLSearchParams({ prefix, per_page: "1000" });
    if (cursor) query.set("cursor", cursor);
    const page = await (await request(accountId, token, bucket, `/objects?${query}`)).json();
    for (const object of page.result ?? []) keys.push(object.key);
    if (!page.result_info?.is_truncated) return keys.sort();
    cursor = page.result_info.cursor;
  }
}

export async function readBucket({ accountId, token, bucket }) {
  const deleted = new Set((await list(accountId, token, bucket, "deleted/"))
    .map((key) => key.slice("deleted/".length)));
  const batches = [];
  for (const key of await list(accountId, token, bucket, "raw/")) {
    const [, installId] = key.split("/");
    if (!installId || deleted.has(installId)) continue;
    const batch = await (await request(accountId, token, bucket, `/objects/${encodeURIComponent(key)}`)).json();
    if (batch.installId !== installId) continue;
    batches.push(batch);
  }
  return { batches, deleted };
}

function argument(args, name, fallback) {
  const index = args.indexOf(name);
  if (index < 0) return fallback;
  const value = args[index + 1];
  if (value === undefined) throw new Error(`${name} needs a value`);
  return value;
}

export async function main(argv = process.argv.slice(2)) {
  const accountId = process.env.CLOUDFLARE_ACCOUNT_ID;
  const token = process.env.CLOUDFLARE_API_TOKEN;
  const bucket = process.env.CONTRIBUTE_BUCKET ?? "sonicator-contribute";
  if (!accountId || !token) {
    console.error("CLOUDFLARE_ACCOUNT_ID and CLOUDFLARE_API_TOKEN are required");
    return 2;
  }
  const out = argument(argv, "--out", "contribute-export.jsonl");
  const minInstalls = Number(argument(argv, "--min-installs", String(DEFAULT_MIN_INSTALLS)));
  if (!Number.isInteger(minInstalls) || minInstalls < 1) throw new Error("--min-installs must be a positive integer");

  const { batches, deleted } = await readBucket({ accountId, token, bucket });
  const records = latestRecords(batches);
  const { summary, report } = await writeExport(records, out, { minInstalls });

  console.log(`wrote ${out}`);
  console.log(`batches read: ${batches.length}, installs: ${summary.installs}, deleted installs: ${deleted.size}`);
  console.log(`records: ${summary.records}`);
  console.log(`report: ${report}`);
  if (argv.includes("--stats")) console.log(JSON.stringify(summary, null, 2));
  return 0;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href)
  main().then((code) => process.exit(code)).catch((error) => {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  });
