// A common scale for the two sources of evidence. A raw local cosine and a softmax probability are
// not comparable numbers: the app's thresholds are cosine thresholds, while the public model's score
// is a calibrated probability. Both are mapped to the precision they actually carry, so the 0.80
// acceptance threshold means "about 80% correct" for either source, and the merge can choose between
// them on merit instead of on scale.

import { buildLocalIndex, localRanked } from "./local.mjs";

function poolAdjacent(samples) {
  const blocks = [];
  for (const sample of samples) {
    const last = blocks.at(-1);
    if (last !== undefined && Math.abs(last.x - sample.x) < 1e-12) {
      last.hits += sample.hit;
      last.count += 1;
    } else {
      blocks.push({ x: sample.x, hits: sample.hit, count: 1 });
    }
  }
  for (let i = 0; i < blocks.length; i += 1) blocks[i].precision = blocks[i].hits / blocks[i].count;

  const pooled = [];
  for (const block of blocks) {
    pooled.push({ ...block });
    while (pooled.length > 1
      && pooled[pooled.length - 2].precision > pooled[pooled.length - 1].precision + 1e-12) {
      const right = pooled.pop();
      const left = pooled.pop();
      pooled.push({
        x: left.x,
        hits: left.hits + right.hits,
        count: left.count + right.count,
        precision: (left.hits + right.hits) / (left.count + right.count),
      });
    }
  }

  const map = pooled.map((block) => [block.x, Math.min(1, Math.max(0, block.precision))]);
  if (map[0][0] > 0) map.unshift([0, map[0][1]]);
  if (map.at(-1)[0] < 1) map.push([1, map.at(-1)[1]]);
  map[0] = [0, Math.min(map[0][1], map[1]?.[1] ?? 1)];
  return map;
}

// samples: [{ x, hit }] where x is a score and hit is 1 when the prediction was correct.
export function isotonicMap(samples) {
  if (samples.length === 0) return [[0, 0], [1, 1]];
  const sorted = [...samples].sort((a, b) => a.x - b.x || a.hit - b.hit);
  return poolAdjacent(sorted);
}

export function applyMap(map, x) {
  if (!Array.isArray(map) || map.length === 0) return x;
  const value = Math.min(1, Math.max(0, x));
  if (value <= map[0][0]) return map[0][1];
  for (let i = 1; i < map.length; i += 1) {
    const [rightX, rightP] = map[i];
    if (value <= rightX) {
      const [leftX, leftP] = map[i - 1];
      if (rightX - leftX < 1e-12) return rightP;
      const t = (value - leftX) / (rightX - leftX);
      return leftP + (rightP - leftP) * t;
    }
  }
  return map.at(-1)[1];
}

// Folds are cut by recordId, not by row: two copies of the same audio always land in the same fold,
// so a copy can never train the model that scores its twin. Every score in the result is out of fold.
export function fitLocalCalibration(rows, { indices, folds = 5 } = {}) {
  const ids = [...new Set(rows.map((row) => row.recordId))].sort();
  const foldOf = new Map(ids.map((id, index) => [id, index % Math.max(1, folds)]));
  const samples = [];
  for (let fold = 0; fold < Math.max(1, folds); fold += 1) {
    const train = rows.filter((row) => foldOf.get(row.recordId) !== fold);
    const test = rows.filter((row) => foldOf.get(row.recordId) === fold);
    if (train.length === 0 || test.length === 0) continue;
    const index = buildLocalIndex(train, indices);
    for (const row of test) {
      const ranked = localRanked(row, index, indices);
      if (ranked.length === 0) continue;
      samples.push({ x: ranked[0].score, hit: ranked[0].label === row.label ? 1 : 0 });
    }
  }
  return isotonicMap(samples);
}
