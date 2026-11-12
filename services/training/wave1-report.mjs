import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

function count(map, key) {
  map.set(key, (map.get(key) ?? 0) + 1);
}

function toObject(map) {
  return Object.fromEntries([...map.entries()].sort((a, b) => (a[0] < b[0] ? -1 : 1)));
}

export function summarise({ clips, rows }) {
  const records = rows.map((row, index) => (row.source !== undefined ? row : { ...clips[index], ...row }));
  const perSource = new Map();
  const perSplit = new Map();
  const perGroup = new Map();
  const perDomain = new Map();
  const licenses = new Map();
  const unmapped = new Map();
  const labels = new Map();
  const bands = { below01: 0, below05: 0, atLeast05: 0 };
  let lowBandShare = 0;

  for (const record of records) {
    count(perSource, record.source ?? "?");
    count(perSplit, record.split ?? "?");
    count(perGroup, record.group ?? "?");
    count(perDomain, record.domain ?? "?");
    count(licenses, record.license ?? "?");
    if (record.label === null || record.label === undefined) count(unmapped, record.source ?? "?");
    else count(labels, record.label);
    const share = record.inBandEnergyShare;
    if (typeof share === "number") {
      if (share < 0.1) {
        lowBandShare += 1;
        bands.below01 += 1;
      } else if (share < 0.5) bands.below05 += 1;
      else bands.atLeast05 += 1;
    }
  }

  return {
    total: records.length,
    perSource: toObject(perSource),
    perSplit: toObject(perSplit),
    perGroup: toObject(perGroup),
    perDomain: toObject(perDomain),
    licenses: toObject(licenses),
    unmapped: toObject(unmapped),
    labels: toObject(labels),
    lowBandShare,
    bands,
  };
}

export function renderReport(summary, extra = {}) {
  const lines = [];
  const table = (title, object) => {
    lines.push(`## ${title}`, "", "| key | clips |", "|---|---|");
    for (const [key, value] of Object.entries(object).sort((a, b) => (a[0] < b[0] ? -1 : 1)))
      lines.push(`| ${key} | ${value} |`);
    lines.push("");
  };
  lines.push("# Волна 1: состав набора", "", `Строк: **${summary.total}**`, "");
  table("Источники", summary.perSource);
  table("Сплиты", summary.perSplit);
  table("Группы", summary.perGroup);
  table("Домены", summary.perDomain);
  table("Лицензии", summary.licenses);
  table("Без метки (label: null)", summary.unmapped);
  lines.push("## Полоса", "", `Ниже 0.1 доли энергии в 40–8000 Гц: **${summary.lowBandShare}**`, "",
    `- < 0.1: ${summary.bands.below01}`, `- 0.1–0.5: ${summary.bands.below05}`, `- ≥ 0.5: ${summary.bands.atLeast05}`, "");
  lines.push("## Метки", "", "| label | clips |", "|---|---|");
  const labels = Object.entries(summary.labels).sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : 1));
  for (const [label, value] of labels) lines.push(`| ${label} | ${value} |`);
  lines.push("");
  if (extra.skipped !== undefined) {
    lines.push("## Отброшено при ингесте", "", "```json", JSON.stringify(extra.skipped, null, 2), "```", "");
  }
  if (extra.unmatched !== undefined) lines.push(`Строк признаков без разметки: **${extra.unmatched}**.`, "");
  return lines.join("\n");
}

function readJsonLines(file) {
  return readFileSync(file, "utf8")
    .split("\n")
    .filter((line) => line.length > 0)
    .map((line) => JSON.parse(line));
}

function main() {
  const argv = process.argv.slice(2);
  const options = { in: null, doc: null };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === "--in") options.in = argv[++i];
    else if (argv[i] === "--doc") options.doc = argv[++i];
    else throw new Error(`unknown argument: ${argv[i]}`);
  }
  if (options.in === null) throw new Error("--in is required");
  const directory = resolve(options.in);
  const clips = readJsonLines(join(directory, "clips.jsonl"));
  const rows = readJsonLines(join(directory, "features.jsonl"));
  const build = existsSync(join(directory, "build.json"))
    ? JSON.parse(readFileSync(join(directory, "build.json"), "utf8"))
    : {};
  const summary = summarise({ clips, rows });
  writeFileSync(join(directory, "summary.json"), `${JSON.stringify(summary, null, 2)}\n`);
  const report = renderReport(summary, { skipped: build.skipped, unmatched: build.unmatched });
  writeFileSync(join(directory, "report.md"), report);
  if (options.doc !== null) writeFileSync(resolve(options.doc), report);
  process.stdout.write(`wave 1: ${summary.total} rows, ${summary.lowBandShare} below 0.1 band share\n`);
}

const invokedDirectly = process.argv[1] !== undefined
  && pathToFileURL(resolve(process.argv[1])).href === import.meta.url;
if (invokedDirectly) main();
