#!/usr/bin/env node
// Lists custom labels that many installs typed, so the owner can promote a few into the standard
// vocabulary. Nothing is applied automatically: a promotion changes what every catalog shows.

import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname } from "node:path";
import process from "node:process";
import { pathToFileURL } from "node:url";
import { normalizeRecords, parseDataset } from "./lib.mjs";
import { STANDARD_TAGS, STANDARD_TYPES } from "./standard.mjs";

export const MIN_INSTALLS = 5;
export const MIN_INSTALL_SHARE = 0.01;

function looksLikeJunk(text) {
  const trimmed = String(text).trim();
  if (trimmed.length < 2 || trimmed.length > 32) return true;
  if (/[/\\@]/.test(trimmed) || trimmed.includes("://")) return true;
  if (/^\d+([.,]\d+)?$/.test(trimmed)) return true;
  if (!/[\p{L}]/u.test(trimmed)) return true;
  return false;
}

function standardLookup() {
  const tags = new Map(STANDARD_TAGS.map((name) => [name.toLowerCase(), name]));
  const types = new Map(STANDARD_TYPES.map((name) => [name.toLowerCase(), name]));
  return { type: types, tag: tags };
}

export function candidates(records, installs, { minInstalls = MIN_INSTALLS, share = MIN_INSTALL_SHARE } = {}) {
  const standard = standardLookup();
  const seen = new Map();
  for (const record of normalizeRecords(records)) {
    const entries = [];
    if (record.typeIsCustom) entries.push({ kind: "type", name: record.type });
    for (const tag of record.tags ?? []) {
      if (tag?.custom) entries.push({ kind: "tag", name: tag.name });
    }
    for (const entry of entries) {
      const key = `${entry.kind}:${String(entry.name).toLowerCase()}`;
      const item = seen.get(key) ?? { kind: entry.kind, name: String(entry.name).trim(), records: 0, installs: new Set() };
      item.records += 1;
      item.installs.add(record.installId);
      seen.set(key, item);
    }
  }

  const minimum = Math.max(1, Math.ceil(installs * share));
  const out = [];
  for (const item of seen.values()) {
    if (item.installs.size < minInstalls || item.installs.size < minimum) continue;
    if (looksLikeJunk(item.name)) continue;
    const existing = standard[item.kind].get(item.name.toLowerCase());
    out.push({
      kind: item.kind,
      label: item.name,
      records: item.records,
      installSupport: item.installs.size,
      share: installs === 0 ? 0 : Math.round((item.installs.size / installs) * 1000) / 1000,
      suggestion: existing ? `merge into ${existing}` : `new ${item.kind}`,
    });
  }
  return out.sort((a, b) =>
    b.installSupport - a.installSupport || b.records - a.records || a.label.localeCompare(b.label));
}

export function renderCandidates(list, { installs, records, minInstalls = MIN_INSTALLS } = {}) {
  const rows = list.map((item) =>
    [item.label, item.kind, item.records, item.installSupport, `${(item.share * 100).toFixed(1)}%`, item.suggestion]);
  return `# Standard tag and type candidates

Generated from ${records ?? 0} contributed records across ${installs ?? 0} installs.
A candidate needs at least ${minInstalls} installs and 1% of installs, and must not look like a
path, an address, a number or an unusable fragment. Promotion is manual; see
\`docs/TAG_PROMOTION.md\`.

${rows.length === 0
    ? "No candidate reached the threshold yet."
    : `| Label | Kind | Records | Installs | Share | Suggestion |\n|---|---|---|---|---|---|\n${rows
        .map((row) => `| ${row.join(" | ")} |`)
        .join("\n")}`}
`;
}

export async function main(argv = process.argv.slice(2)) {
  const input = valueOf(argv, "--in", "contribute-export.jsonl");
  const markdown = valueOf(argv, "--out", "../../docs/tag-candidates.md");
  const json = valueOf(argv, "--json", "out/candidates.json");
  const records = parseDataset(await readFile(input, "utf8"));
  const installs = new Set(records.map((record) => record.installId)).size;
  const list = candidates(records, installs, { minInstalls: Number(valueOf(argv, "--min-installs", MIN_INSTALLS)) });

  await mkdir(dirname(markdown), { recursive: true });
  await mkdir(dirname(json), { recursive: true });
  await writeFile(markdown, renderCandidates(list, { installs, records: records.length }));
  await writeFile(json, `${JSON.stringify({ installs, records: records.length, candidates: list }, null, 2)}\n`);
  console.log(`candidates: ${list.length}, installs: ${installs}, records: ${records.length}`);
  console.log(`report: ${markdown}`);
  console.log(`data: ${json}`);
  return 0;
}

function valueOf(argv, name, fallback) {
  const index = argv.indexOf(name);
  if (index < 0) return fallback;
  const value = argv[index + 1];
  if (value === undefined) throw new Error(`${name} needs a value`);
  return value;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href)
  main().then((code) => process.exit(code)).catch((error) => {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(2);
  });
