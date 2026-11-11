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
  buildPrototypes,
  indexPrototypes,
  indexRecords,
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

// Folds are cut inside each install, by recordId order, so every fold holds records of every
// install and no record is ever scored against a model built from itself.
function assignFolds(records, folds) {
  const byInstall = new Map();
  for (const record of records) {
    const list = byInstall.get(record.installId) ?? [];
    list.push(record);
    byInstall.set(record.installId, list);
  }
  const assigned = new Map();
  for (const list of byInstall.values()) {
    const sorted = [...list].sort((a, b) => (a.recordId < b.recordId ? -1 : a.recordId > b.recordId ? 1 : 0));
    sorted.forEach((record, index) => assigned.set(record.recordId, index % folds));
  }
  return assigned;
}

// What a user actually gets from the file we ship: prototypes built without the record under test,
// scored with the same global penalty the app applies. Custom types are not in the shipped file, so
// they are left out of the number rather than counted as misses.
export function crossValidateShipped(records, { folds = 5 } = {}) {
  const normalized = normalizeRecords(records);
  const foldCount = Math.max(1, Math.floor(folds));
  const assigned = assignFolds(normalized, foldCount);
  const result = {
    folds: foldCount,
    records: 0,
    correct: 0,
    accuracy: 0,
    groups: new Map(),
    labels: new Map(),
  };

  for (let fold = 0; fold < foldCount; fold += 1) {
    const train = normalized.filter((record) => assigned.get(record.recordId) !== fold);
    const test = normalized.filter((record) => assigned.get(record.recordId) === fold);
    if (test.length === 0) continue;
    const globalIndex = indexPrototypes(buildPrototypes(train, { minInstalls: 1, minSupport: 1 }));

    for (const record of test) {
      if (!record.type || record.typeIsCustom) continue;
      const truth = record.type.toLowerCase();
      const prediction = predictType(record.features, emptyIndex, globalIndex);
      const hit = prediction?.label.toLowerCase() === truth;
      result.records += 1;
      if (hit) result.correct += 1;

      const group = groupForType(record.type);
      const groupEntry = result.groups.get(group) ?? { records: 0, correct: 0 };
      groupEntry.records += 1;
      if (hit) groupEntry.correct += 1;
      result.groups.set(group, groupEntry);

      const labelEntry = result.labels.get(truth) ?? { label: record.type, records: 0, correct: 0 };
      labelEntry.records += 1;
      if (hit) labelEntry.correct += 1;
      result.labels.set(truth, labelEntry);
    }
  }

  result.accuracy = result.records === 0 ? 0 : result.correct / result.records;
  return result;
}

function percent(value) {
  return `${(value * 100).toFixed(1)}%`;
}

// Same index without the vector of the record under test, matched by reference: the record must
// not be its own prototype, and a twin record with an identical vector must not stand in for it.
function withoutOwnVector(index, features) {
  const out = new Map();
  for (const [kind, labels] of index) {
    const copy = new Map();
    for (const [key, entry] of labels) {
      const prototypes = entry.prototypes.filter((vector) => vector !== features);
      if (prototypes.length > 0) copy.set(key, { ...entry, prototypes });
    }
    out.set(kind, copy);
  }
  return out;
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
    const localIndex = indexRecords(localRecords);

    for (const record of heldOut) {
      const localForRecord = withoutOwnVector(localIndex, record.features);
      // A custom type is the person's own name, not a label the shipped model carries; it still
      // trains the local prototype exactly as the app does, so only its accuracy is left out.
      const hasType = Boolean(record.type) && !record.typeIsCustom;
      if (hasType) {
        const truthType = record.type.toLowerCase();
        const beforeType = predictType(record.features, localForRecord, emptyIndex);
        const afterType = predictType(record.features, localForRecord, globalIndex);
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
      }

      const truthTags = new Set(tagNames(record).map((tag) => tag.toLowerCase()));
      const beforeTags = predictTags(record.features, localForRecord, emptyIndex);
      const afterTags = predictTags(record.features, localForRecord, globalIndex);
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

export function renderReport(summary, { generatedAt, crossValidation } = {}) {
  const groupRows = summary.type.groups
    .map((group) => [group.group, group.records, percent(group.before), percent(group.after)])
    .map((row) => `| ${row.join(" | ")} |`);
  const worstRows = summary.type.worst
    .map((label) => [label.label, label.records, percent(label.after)])
    .map((row) => `| ${row.join(" | ")} |`);
  const shipped = crossValidation ? renderShippedSection(crossValidation) : "";
  return `# Training evaluation

Generated: ${generatedAt ?? "unknown"}

- Installs held out in turn: ${summary.installs}
- Records evaluated: ${summary.records}
- Records with a standard type: ${summary.type.records}
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

Global tag prototypes are in the file but this build does not load them, so the "after" column is a
preview for a later stage, not behaviour a user gets today.

| | Precision | Recall |
|---|---|---|
| Before | ${percent(summary.tags.beforePrecision)} | ${percent(summary.tags.beforeRecall)} |
| After | ${percent(summary.tags.afterPrecision)} | ${percent(summary.tags.afterRecall)} |
${shipped}`;
}

// The number that answers "what does the shipped file actually get": no local confirmations, no
// record scored against a prototype built from itself.
function renderShippedSection(crossValidation) {
  const groupRows = [...crossValidation.groups.entries()]
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(([group, entry]) => `| ${group} | ${entry.records} | ${percent(entry.correct / entry.records)} |`);
  const worstRows = [...crossValidation.labels.values()]
    .filter((label) => label.records >= 5)
    .sort((a, b) => a.correct / a.records - b.correct / b.records
      || b.records - a.records
      || a.label.localeCompare(b.label))
    .slice(0, 5)
    .map((label) => `| ${label.label} | ${label.records} | ${percent(label.correct / label.records)} |`);
  return `
## Shipped prototypes, cross-validated

The record under test is never part of the prototypes it is scored against. Folds are cut inside
each install and the global penalty of 0.95 applies, as in the app.

- Folds: ${crossValidation.folds}
- Records: ${crossValidation.records}
- Accuracy: ${percent(crossValidation.accuracy)}

### By group

| Group | Records | Accuracy |
|---|---|---|
${groupRows.length > 0 ? groupRows.join("\n") : "| (no record) | | |"}

### Worst labels

| Type | Records | Accuracy |
|---|---|---|
${worstRows.length > 0 ? worstRows.join("\n") : "| (no label with at least 5 records) | | |"}
`;
}

export async function main(argv = process.argv.slice(2)) {
  const input = valueOf(argv, "--in", "contribute-export.jsonl");
  const output = valueOf(argv, "--out", "training-report.md");
  const folds = Number(valueOf(argv, "--cv", 5));
  const records = parseDataset(await readFile(input, "utf8"));
  const result = evaluate(records);
  const summary = summarizeEvaluation(result);
  const crossValidation = folds > 0 ? crossValidateShipped(records, { folds }) : undefined;
  const generatedAt = valueOf(argv, "--generated-at", undefined)
    ?? new Date(Math.max(0, ...records.map((record) => record.updatedAt ?? 0))).toISOString();
  await mkdir(dirname(output), { recursive: true });
  await writeFile(output, renderReport(summary, { generatedAt, crossValidation }));
  console.log(`records: ${summary.records}, installs: ${summary.installs}`);
  console.log(`type accuracy before ${percent(summary.type.before)}, after ${percent(summary.type.after)}`);
  if (crossValidation)
    console.log(`shipped prototypes, ${crossValidation.folds}-fold cv: ${percent(crossValidation.accuracy)}`);
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
