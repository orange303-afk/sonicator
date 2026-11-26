#!/usr/bin/env node
// Measures the model that ships in resources/ml/global_model.json against contributed records, with no
// training and no tuning. Point it at a synced export (data/contributions/latest/contribute.jsonl) and
// at a candidate model file; it writes a summary JSON and a readable report.
//
// Usage:
//   node evaluate-shipped.mjs [--in FILE] [--model FILE] [--out DIR] [--doc FILE]
//
// Exit code 0 means the report was written. The numbers are a decision for the owner, not a gate.

import { mkdirSync, writeFileSync } from "node:fs";
import { join, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { assertOutAllowed } from "./build-features.mjs";
import { evaluateContributed, loadShippedModel, readContributedRows } from "./heldout.mjs";
import { renderHeldOutReport } from "./heldout-report.mjs";

const REPOSITORY = fileURLToPath(new URL("../..", import.meta.url));

function argument(argv, name, fallback) {
  const index = argv.indexOf(name);
  if (index < 0) return fallback;
  const value = argv[index + 1];
  if (value === undefined) throw new Error(`${name} needs a value`);
  return value;
}

export function main(argv = process.argv.slice(2)) {
  const input = resolve(argument(argv, "--in", join(REPOSITORY, "data", "contributions", "latest", "contribute.jsonl")));
  const modelFile = resolve(argument(argv, "--model", join(REPOSITORY, "resources", "ml", "global_model.json")));
  const out = resolve(argument(argv, "--out", join(REPOSITORY, "services", "training", "out", "heldout")));
  const docArgument = argument(argv, "--doc", null);
  const doc = docArgument === null ? null : resolve(docArgument);
  assertOutAllowed(out);

  const shipped = loadShippedModel(modelFile);
  const data = readContributedRows(input);
  const typed = data.rows.filter((row) => row.labelSource === "type");
  const tagged = data.rows.filter((row) => row.labelSource === "tag");
  // "prefer-local" is the rule the app used before the schema-2 fix; "global-only" is the shipped
  // rule. Both run on the same rows so the report can show what the change did.
  const summary = evaluateContributed({ rows: typed, shipped, localPolicy: "prefer-local" });
  const tagSummary = tagged.length === 0
    ? null
    : evaluateContributed({ rows: tagged, prototypes: typed, shipped, localPolicy: "prefer-local" });
  const modelOnly = {
    summary: evaluateContributed({ rows: typed, shipped, localPolicy: "global-only" }),
    tags: tagged.length === 0
      ? null
      : evaluateContributed({ rows: tagged, prototypes: typed, shipped, localPolicy: "global-only" }),
  };

  const snapshot = relative(REPOSITORY, input).replaceAll("\\", "/");
  const modelPath = relative(REPOSITORY, modelFile).replaceAll("\\", "/");
  const fromTraining = (target) => relative(join(REPOSITORY, "services", "training"), target).replaceAll("\\", "/");
  const command = [
    "cd services/training && node evaluate-shipped.mjs",
    `--in "${fromTraining(input)}"`,
    `--model "${fromTraining(modelFile)}"`,
    doc === null ? "" : `--doc "${fromTraining(doc)}"`,
  ].filter((part) => part.length > 0).join(" ");

  mkdirSync(out, { recursive: true });
  writeFileSync(
    join(out, "summary.json"),
    `${JSON.stringify({ snapshot, stats: data.stats, summary, tags: tagSummary, modelOnly }, null, 2)}\n`,
    "utf8",
  );
  if (doc !== null) {
    mkdirSync(resolve(doc, ".."), { recursive: true });
    writeFileSync(doc, renderHeldOutReport({ summary, tagSummary, modelOnly, data, snapshot, modelPath, command }), "utf8");
  }

  const cold = summary.standard.cold;
  const app = summary.standard.app;
  const modelApp = modelOnly.summary.standard.app;
  const tagCold = tagSummary === null ? null : tagSummary.standard.cold;
  process.stdout.write(
    `${summary.dataset.records} confirmed types from ${summary.installs.length} install(s), `
    + `${summary.labels.length} labels\n`
    + `cold start: top-1 ${(cold.top1 * 100).toFixed(1)}%, coverage ${(cold.coverage * 100).toFixed(1)}%, `
    + `precision ${(cold.precision * 100).toFixed(1)}%\n`
    + `with own confirmations: top-1 ${(app.top1 * 100).toFixed(1)}%, coverage ${(app.coverage * 100).toFixed(1)}%, `
    + `precision ${(app.precision * 100).toFixed(1)}%\n`
    + (tagCold === null
      ? "tag-derived labels: none in this export\n"
      : `tag-derived labels: ${tagSummary.dataset.records} rows, cold start top-1 `
        + `${(tagCold.top1 * 100).toFixed(1)}%, coverage ${(tagCold.coverage * 100).toFixed(1)}%, `
        + `precision ${(tagCold.precision * 100).toFixed(1)}%\n`)
    + `model alone (shipped policy): top-1 ${(modelApp.top1 * 100).toFixed(1)}%, `
    + `coverage ${(modelApp.coverage * 100).toFixed(1)}%, precision ${(modelApp.precision * 100).toFixed(1)}%\n`
    + `summary: ${join(out, "summary.json")}${doc === null ? "" : `\nreport: ${doc}`}\n`
  );
  return 0;
}

const invokedDirectly = process.argv[1] !== undefined
  && pathToFileURL(resolve(process.argv[1])).href === import.meta.url;
if (invokedDirectly) {
  try {
    process.exitCode = main();
  } catch (error) {
    process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n`);
    process.exitCode = 2;
  }
}
