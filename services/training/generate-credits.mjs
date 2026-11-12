#!/usr/bin/env node
// Writes docs/DATA_CREDITS.md from the dataset registry. The file is committed, so the repository
// carries the attribution the licences require without anyone having to remember it.

import { mkdir, writeFile } from "node:fs/promises";
import { dirname } from "node:path";
import process from "node:process";
import { pathToFileURL } from "node:url";
import { loadManifest } from "./datasets.mjs";

const DEFAULT_OUTPUT = new URL("../../docs/DATA_CREDITS.md", import.meta.url);
const COMPARE = (a, b) => (a < b ? -1 : a > b ? 1 : 0);

function tableRow(cells) {
  return `| ${cells.join(" | ")} |`;
}

// No timestamp and no machine paths, so the committed file is reproducible from the registry.
export function renderCredits(entries) {
  const byId = [...entries].sort((a, b) => COMPARE(a.id, b.id));
  const used = byId.filter((entry) => entry.used);
  const unused = byId.filter((entry) => !entry.used);
  const withCounts = used.some((entry) => entry.clipsUsed !== undefined);
  const header = withCounts
    ? ["id", "Title", "Version", "Licence", "Level", "Clips used"]
    : ["id", "Title", "Version", "Licence", "Level"];
  const separator = header.map(() => "---");

  const lines = [
    "# Data credits",
    "",
    "Generated from `services/training/datasets/manifest.json` by",
    "`services/training/generate-credits.mjs`; do not edit by hand.",
    "",
    "## Datasets used for the shipped model and its evaluation",
    "",
    tableRow(header),
    tableRow(separator),
    ...used.map((entry) => tableRow(withCounts
      ? [entry.id, entry.title, entry.version, entry.license, entry.tier, String(entry.clipsUsed ?? "—")]
      : [entry.id, entry.title, entry.version, entry.license, entry.tier])),
    "",
    "## Attribution",
    "",
    "Licences that require attribution name the work as follows.",
    "",
    ...used.map((entry) => `- ${entry.creditLine} — ${entry.version}, ${entry.url}`),
    "",
    "## Not used",
    "",
    ...unused.map((entry) =>
      `- **${entry.title}** (${entry.id}), level ${entry.tier}: ${entry.notes ?? "excluded"}`),
    "",
  ];
  return lines.join("\n");
}

export async function writeCredits({ output = DEFAULT_OUTPUT } = {}) {
  const text = renderCredits(loadManifest());
  if (typeof output === "string") await mkdir(dirname(output), { recursive: true });
  await writeFile(output, text);
  return text;
}

export async function main(argv = process.argv.slice(2)) {
  const index = argv.indexOf("--out");
  const output = index >= 0 ? argv[index + 1] : DEFAULT_OUTPUT;
  if (index >= 0 && output === undefined) throw new Error("--out needs a value");
  await writeCredits({ output });
  console.log(`wrote ${typeof output === "string" ? output : "docs/DATA_CREDITS.md"}`);
  return 0;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href)
  main().then((code) => process.exit(code)).catch((error) => {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(2);
  });
