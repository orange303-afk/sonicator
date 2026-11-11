#!/usr/bin/env node
// Rebuilds resources/ml/global_prototypes.json from an exported dataset.
//
// The file is committed before a release, so the app never downloads anything. With too little
// data it is still a valid file with an empty label list, and the app behaves as it does today.

import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname } from "node:path";
import process from "node:process";
import { pathToFileURL } from "node:url";
import {
  MAX_LABELS_PER_KIND,
  MAX_PROTOTYPES_PER_LABEL,
  MIN_INSTALLS,
  MIN_SUPPORT,
  buildPrototypes,
  parseDataset,
  serializePrototypes,
} from "./lib.mjs";

export async function main(argv = process.argv.slice(2)) {
  const input = valueOf(argv, "--in", "contribute-export.jsonl");
  const output = valueOf(argv, "--out", "../../resources/ml/global_prototypes.json");
  const records = parseDataset(await readFile(input, "utf8"));
  const file = buildPrototypes(records, {
    generatedAt: valueOf(argv, "--generated-at", undefined),
    minInstalls: Number(valueOf(argv, "--min-installs", MIN_INSTALLS)),
    minSupport: Number(valueOf(argv, "--min-support", MIN_SUPPORT)),
  });
  await mkdir(dirname(output), { recursive: true });
  await writeFile(output, serializePrototypes(file));
  console.log(`records: ${file.records}, installs: ${file.installs}, labels: ${file.labels.length}`);
  console.log(`per label: up to ${MAX_PROTOTYPES_PER_LABEL} prototypes, up to ${MAX_LABELS_PER_KIND} labels per kind`);
  console.log(`wrote ${output}`);
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
