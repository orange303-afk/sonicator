// Local leave-one-out prototypes over raw feature values: the app stores raw vectors, so the local
// path uses raw cosine exactly as the it does. Shared by the evaluator and the calibration pass.

import { cosine } from "../lib.mjs";

export function vectorOf(row, indices) {
  return indices.map((index) => row.features[index]);
}

export function buildLocalIndex(rows, indices) {
  const index = new Map();
  for (const row of rows) {
    if (!row.label) continue;
    const list = index.get(row.label) ?? [];
    list.push({ row, vector: vectorOf(row, indices) });
    index.set(row.label, list);
  }
  return index;
}

function sortedCandidates(entries) {
  return entries.sort((a, b) => b.score - a.score || a.label.localeCompare(b.label));
}

// The row under test is dropped by identity and by recordId: a second copy of the same audio content
// must not stand in as the "other" prototype for its twin.
export function localRanked(row, index, indices) {
  const vector = vectorOf(row, indices);
  const entries = [];
  for (const [label, list] of index) {
    let best = 0;
    for (const candidate of list) {
      if (candidate.row === row || candidate.row.recordId === row.recordId) continue;
      best = Math.max(best, cosine(vector, candidate.vector));
    }
    entries.push({ label, score: best, source: "local" });
  }
  return sortedCandidates(entries);
}
