#!/usr/bin/env node
// Leave-one-install-out evaluation of the shipped prototypes against today's local-only behaviour.
//
// The comparison repeats what the app does: local prototypes come from the install's own
// confirmations, global prototypes from every other install, the global score is multiplied by
// 0.95 before the acceptance threshold, and a local label wins within 0.02. The record under test
// is never one of its own prototypes, so the numbers measure prediction and not memorisation.

import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname } from "node:path";
import process from "node:process";
import { pathToFileURL } from "node:url";
import {
  TAG_THRESHOLD,
  TYPE_THRESHOLD,
  buildIndex,
  normalizeRecords,
  parseDataset,
  predictTags,
  predictType,
  tagNames,
} from "./lib.mjs";
import { groupForType } from "./groups.mjs";

class EmptyIndex {
  get() {
    return new Map();
  }
}
const emptyIndex = new EmptyIndex();

function percent(value) {
  return `${(value * 100).toFixed(1)}%`;
}

export function evaluate(records) {
  const normalized = normalizeRecords(records);
  const installs = [...new Set(normalized.map((record) => record.installId))].sort();
  const byInstall = new Map(installs.map((install) => [install, []]));
  for (const record of normalized) byInstall.get(record.installId).push(record);

  const result = {
    records: normalized.length,
    installs: installs.length,
    type: { records: 0, before: 0, after: 0, groups: new Map() },
    tags: { truth: 0, before: 0, after: 0, predictedBefore: 0, predictedAfter: 0 },
    labels: new Map(),
  };

  for (const install of installs) {
    const heldOut = byInstall.get(install);
    const others = normalized.filter((record) => record.installId !== install);
    const globalIndex = buildIndex(others);
    const localRecords = byInstall.get(install);

    for (const record of heldOut) {
      const localIndex = buildIndex(
        localRecords.filter((entry) => entry.recordId !== record.recordId),
        { minInstalls: 1, minSupport: 1 },
      );
      const truthType = record.type.toLowerCase();
      const beforeType = predictType(record.features, localIndex, emptyIndex);
      const afterType = predictType(record.features, localIndex, globalIndex);
      result.type.records += 1;
      if (beforeType?.label.toLowerCase() === truthType) result.type.before += 1;
      if (afterType?.label.toLowerCase() === truthType) result.type.after += 1;

      const group = groupForType(record.type);
      const entry = result.type.groups.get(group) ?? { records: 0, before: 0, after: 0 };
      entry.records += 1;
      if (beforeType?.label.toLowerCase() === truthType) entry.before += 1;
      if (afterType?.label.toLowerCase() === truthType) entry.after += 1;
      result.type.groups.set(group, entry);

      const label = result.labels.get(truthType) ?? { label: record.type, records: 0, after: 0 };
      label.records += 1;
      if (afterType?.label.toLowerCase() === truthType) label.after += 1;
      result.labels.set(truthType, label);

      const truthTags = new Set(tagNames(record).map((tag) => tag.toLowerCase()));
      const beforeTags = predictTags(record.features, localIndex, emptyIndex);
      const afterTags = predictTags(record.features, localIndex, globalIndex);
      result.tags.truth += truthTags.size;
      result.tags.predictedBefore += beforeTags.length;
      result.tags.predictedAfter += afterTags.length;
      for (const tag of beforeTags)
        if (truthTags.has(tag.label.toLowerCase())) result.tags.before += 1;
      for (const tag of afterTags)
        if (truthTags.has(tag.label.toLowerCase())) result.tags.after += 1;
    }
  }
  return result;
}

function safeRatio(numerator, denominator) {
  return denominator === 0 ? 0 : numerator / denominator;
}

export function summarizeEvaluation(result) {
  const before = safeRatio(result.type.before, result.type.records);
  const after = safeRatio(result.type.after, result.type.records);
  const groups = [...result.type.groups.entries()]
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(([group, entry]) => ({
      group,
      records: entry.records,
      before: safeRatio(entry.before, entry.records),
      after: safeRatio(entry.after, entry.records),
    }));
  const worst = [...result.labels.values()]
    .filter((label) => label.records >= 5)
    .map((label) => ({
      label: label.label,
      records: label.records,
      after: safeRatio(label.after, label.records),
    }))
    .sort((a, b) => a.after - b.after || b.records - a.records || a.label.localeCompare(b.label))
    .slice(0, 10);
  return {
    records: result.records,
    installs: result.installs,
    type: {
      before,
      after,
      thresholdUsed: TYPE_THRESHOLD,
      tagThresholdUsed: TAG_THRESHOLD,
      gate: after >= 0.6 && after + 1e-12 >= before,
      groups,
      worst,
    },
    tags: {
      beforePrecision: safeRatio(result.tags.before, result.tags.predictedBefore),
      beforeRecall: safeRatio(result.tags.before, result.tags.truth),
      afterPrecision: safeRatio(result.tags.after, result.tags.predictedAfter),
      afterRecall: safeRatio(result.tags.after, result.tags.truth),
    },
  };
}

export function renderReport(summary, { generatedAt } = {}) {
  const groupRows = summary.type.groups
    .map((group) => [group.group, group.records, percent(group.before), percent(group.after)])
    .map((row) => `| ${row.join(" | ")} |`);
  const worstRows = summary.type.worst
    .map((label) => [label.label, label.records, percent(label.after)])
    .map((row) => `| ${row.join(" | ")} |`);
  return `# Training evaluation

Generated: ${generatedAt ?? "unknown"}

- Installs held out in turn: ${summary.installs}
- Records evaluated: ${summary.records}
- Type acceptance threshold: ${summary.type.thresholdUsed}; tag threshold: ${summary.type.tagThresholdUsed}

## Type, top-1

| | Accuracy |
|---|---|
| Before (local prototypes only) | ${percent(summary.type.before)} |
| After (local + global, penalty 0.95, local within 0.02) | ${percent(summary.type.after)} |

Threshold for shipping without the owner: after ≥ 60% and after ≥ before. Current result:
**${summary.type.gate ? "passes" : "needs the owner's decision"}**.

### By group

| Group | Records | Before | After |
|---|---|---|---|
${groupRows.join("\n")}

### Worst labels after

| Type | Records | Accuracy |
|---|---|---|
${worstRows.length > 0 ? worstRows.join("\n") : "| (no label with at least 5 records) | | |"}

## Tags, top-3 micro

| | Precision | Recall |
|---|---|---|
| Before | ${percent(summary.tags.beforePrecision)} | ${percent(summary.tags.beforeRecall)} |
| After | ${percent(summary.tags.afterPrecision)} | ${percent(summary.tags.afterRecall)} |
`;
}

export async function main(argv = process.argv.slice(2)) {
  const input = valueOf(argv, "--in", "contribute-export.jsonl");
  const output = valueOf(argv, "--out", "training-report.md");
  const records = parseDataset(await readFile(input, "utf8"));
  const result = evaluate(records);
  const summary = summarizeEvaluation(result);
  const generatedAt = valueOf(argv, "--generated-at", undefined)
    ?? new Date(Math.max(0, ...records.map((record) => record.updatedAt ?? 0))).toISOString();
  await mkdir(dirname(output), { recursive: true });
  await writeFile(output, renderReport(summary, { generatedAt }));
  console.log(`records: ${summary.records}, installs: ${summary.installs}`);
  console.log(`type accuracy before ${percent(summary.type.before)}, after ${percent(summary.type.after)}`);
  console.log(`tags precision ${percent(summary.tags.afterPrecision)}, recall ${percent(summary.tags.afterRecall)}`);
  console.log(`gate: ${summary.type.gate ? "passes" : "needs the owner's decision"}`);
  console.log(`report: ${output}`);
  return summary.type.gate ? 0 : 1;
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
