// Gold-sample evaluation in the four modes of part 7.5. The modes differ only in where the evidence
// comes from; the metrics, the denominator and the acceptance threshold are computed the same way, so
// the columns of the report are comparable.
//
// Mode 1: the schema-1 block (0..13), local cosine, leave-one-out — today's behaviour.
// Mode 2: the selected v2 dimensions, local cosine, leave-one-out.
// Mode 3: the public model alone (cold start), on labels present in both datasets.
// Mode 4: local leave-one-out plus the public model, with the app's 0.95 penalty and 0.02 margin.

import { LOCAL_PRIORITY_MARGIN, TYPE_THRESHOLD, cosine, tagNames } from "../lib.mjs";
import { groupForType } from "../groups.mjs";
import { STANDARD_TYPES } from "../standard.mjs";
import { predict } from "./models.mjs";

function vectorOf(row, indices) {
  return indices.map((index) => row.features[index]);
}

function isStandardLabel(row) {
  return typeof row.label === "string" && row.label.length > 0
    && row.typeIsCustom !== true && STANDARD_TYPES.includes(row.label);
}

function sortedCandidates(entries) {
  return entries.sort((a, b) => b.score - a.score || a.label.localeCompare(b.label));
}

function buildLocalIndex(rows, indices) {
  const index = new Map();
  for (const row of rows) {
    if (!row.label) continue;
    const list = index.get(row.label) ?? [];
    list.push({ row, vector: vectorOf(row, indices) });
    index.set(row.label, list);
  }
  return index;
}

// The row under test is dropped by identity, so an identical vector on another row still counts as a
// prototype while the row itself never votes for itself.
function localRanked(row, index, indices) {
  const vector = vectorOf(row, indices);
  const entries = [];
  for (const [label, list] of index) {
    let best = 0;
    for (const candidate of list) {
      if (candidate.row === row) continue;
      best = Math.max(best, cosine(vector, candidate.vector));
    }
    entries.push({ label, score: best, source: "local" });
  }
  return sortedCandidates(entries);
}

export function mergeCandidates(local, global) {
  if (local === null) return global;
  if (global === null) return local;
  if (local.label === global.label) return global.score > local.score ? global : local;
  if (global.score - local.score <= LOCAL_PRIORITY_MARGIN + 1e-12) return local;
  return global;
}

export function labelIntersection(publicLabels, gold) {
  const labels = (publicLabels ?? []).map((entry) => (typeof entry === "string" ? entry : entry.label));
  const inGold = new Set(gold.map((row) => row.label));
  return [...new Set(labels)].filter((label) => inGold.has(label)).sort();
}

function restrict(ranked, allowed) {
  if (allowed === null) return ranked;
  return ranked.filter((entry) => allowed.has(entry.label));
}

export function evaluateGold({ gold, publicModel, scaler, mode, indices, minRowsPerLabel = 10 }) {
  if (![1, 2, 3, 4].includes(mode)) throw new Error(`unknown mode ${mode}`);
  const counts = new Map();
  for (const row of gold)
    if (isStandardLabel(row)) counts.set(row.label, (counts.get(row.label) ?? 0) + 1);
  const mainLabels = new Set([...counts.entries()].filter(([, count]) => count >= minRowsPerLabel).map(([label]) => label));

  const intersection = publicModel ? new Set(labelIntersection(publicModel.labels, gold)) : new Set();
  const main = gold.filter((row) => isStandardLabel(row) && mainLabels.has(row.label));
  const rows = mode === 3 ? main.filter((row) => intersection.has(row.label)) : main;

  const localIndices = mode === 1 ? Array.from({ length: 14 }, (_, index) => index) : indices;
  const index = mode === 1 || mode === 2 || mode === 4 ? buildLocalIndex(gold, localIndices) : new Map();

  const result = {
    mode,
    records: rows.length,
    correct: 0,
    top1: 0,
    top3: 0,
    coverage: 0,
    precisionAtThreshold: 0,
    accepted: 0,
    acceptedCorrect: 0,
    thresholds: { type: TYPE_THRESHOLD, tag: 0.85 },
    perGroup: [],
    perLabel: [],
    confusion: new Map(),
    tail: { records: 0, correct: 0 },
    recordsWithTags: 0,
  };
  const groups = new Map();
  const labels = new Map();

  for (const row of rows) {
    let ranked;
    if (mode === 1 || mode === 2) {
      ranked = localRanked(row, index, localIndices);
    } else if (mode === 3) {
      ranked = restrict(predict(publicModel, scaler, row.features).ranked, intersection);
    } else {
      const local = sortedCandidates(localRanked(row, index, localIndices));
      const global = restrict(predict(publicModel, scaler, row.features).ranked, intersection);
      const localBest = local.length > 0 ? local[0] : null;
      const globalBest = global.length > 0 ? { ...global[0], score: global[0].score * 0.95 } : null;
      const winner = mergeCandidates(localBest, globalBest);
      // One list for top-3: a local candidate keeps the 0.02 bonus the app gives it when sorting.
      const combined = new Map();
      for (const entry of local) combined.set(entry.label, { ...entry, score: entry.score + LOCAL_PRIORITY_MARGIN });
      for (const entry of global) {
        const score = entry.score * 0.95;
        if (!combined.has(entry.label) || combined.get(entry.label).score < score)
          combined.set(entry.label, { ...entry, score, source: "global" });
      }
      ranked = sortedCandidates([...combined.values()]);
      ranked = winner === null ? [] : [winner, ...ranked.filter((entry) => entry.label !== winner.label)];
    }

    const truth = row.label;
    const top = ranked.length > 0 ? ranked[0] : null;
    const hit = top !== null && top.label === truth;
    result.correct += hit ? 1 : 0;
    if (ranked.slice(0, 3).some((entry) => entry.label === truth)) result.top3 += 1;
    if (top !== null && top.score >= TYPE_THRESHOLD) {
      result.accepted += 1;
      if (hit) result.acceptedCorrect += 1;
    }
    if (top !== null) {
      const predicted = result.confusion.get(truth) ?? new Map();
      predicted.set(top.label, (predicted.get(top.label) ?? 0) + 1);
      result.confusion.set(truth, predicted);
    }

    const group = groups.get(groupForType(truth)) ?? { group: groupForType(truth), records: 0, correct: 0 };
    group.records += 1;
    if (hit) group.correct += 1;
    groups.set(group.group, group);

    const label = labels.get(truth) ?? { label: truth, records: 0, correct: 0 };
    label.records += 1;
    if (hit) label.correct += 1;
    labels.set(truth, label);

    if (tagNames(row).length > 0) result.recordsWithTags += 1;
  }

  for (const row of gold) {
    if (!isStandardLabel(row) || mainLabels.has(row.label)) continue;
    result.tail.records += 1;
  }

  result.top1 = result.records === 0 ? 0 : result.correct / result.records;
  result.top3 = result.records === 0 ? 0 : result.top3 / result.records;
  result.coverage = result.records === 0 ? 0 : result.accepted / result.records;
  result.precisionAtThreshold = result.accepted === 0 ? 0 : result.acceptedCorrect / result.accepted;
  result.perGroup = [...groups.values()]
    .map((entry) => ({ ...entry, top1: entry.records === 0 ? 0 : entry.correct / entry.records }))
    .sort((a, b) => a.group.localeCompare(b.group));
  result.perLabel = [...labels.values()]
    .map((entry) => ({ ...entry, top1: entry.records === 0 ? 0 : entry.correct / entry.records }))
    .sort((a, b) => b.records - a.records || b.top1 - a.top1 || a.label.localeCompare(b.label));
  return result;
}
