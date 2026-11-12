// Feature schema 2 for the training stage: the fixed layout of the C++ extractor, the two loaders
// (public wave and the owner's gold sample), and the shared standardisation used by both models.
// Nothing here reads audio: values come from sonicator-features only.

import { readFileSync } from "node:fs";
import { STANDARD_TYPES } from "../standard.mjs";

export const FEATURE_SCHEMA = 2;
export const FEATURE_DIMENSIONS = 84;

// Half-open ranges, in the order of the layout comment in core/dsp/FeatureExtractorV2.h.
export const FEATURE_GROUPS = Object.freeze({
  G1: [0, 14],
  G2: [14, 32],
  G3: [32, 38],
  G4: [38, 64],
  G5: [64, 76],
  G6: [76, 78],
  G7: [78, 82],
  G8: [82, 84],
});

export const MOMENT_INDICES = Object.freeze([16, 17, 25, 26]);

export function indicesFor(groups, { dropMoments = false } = {}) {
  const indices = [];
  for (const name of groups) {
    const range = FEATURE_GROUPS[name];
    if (range === undefined) throw new Error(`unknown feature group ${name}`);
    for (let index = range[0]; index < range[1]; index += 1)
      if (!(dropMoments && MOMENT_INDICES.includes(index))) indices.push(index);
  }
  return indices;
}

function validateLine(line, where) {
  if (line === null || typeof line !== "object" || Array.isArray(line))
    throw new Error(`${where}: line is not an object`);
  if (line.featureSchema !== FEATURE_SCHEMA)
    throw new Error(`${where}: featureSchema ${line.featureSchema} is not ${FEATURE_SCHEMA}`);
  if (line.dimensions !== FEATURE_DIMENSIONS)
    throw new Error(`${where}: dimensions ${line.dimensions} is not ${FEATURE_DIMENSIONS}`);
  if (!Array.isArray(line.features) || line.features.length !== FEATURE_DIMENSIONS)
    throw new Error(`${where}: features has ${line.features?.length} values`);
  for (const value of line.features)
    if (typeof value !== "number" || !Number.isFinite(value) || value < 0 || value > 1)
      throw new Error(`${where}: feature ${value} is outside 0..1`);
  if (line.inBandEnergyShare !== undefined
    && (typeof line.inBandEnergyShare !== "number" || !Number.isFinite(line.inBandEnergyShare)
      || line.inBandEnergyShare < 0 || line.inBandEnergyShare > 1))
    throw new Error(`${where}: inBandEnergyShare ${line.inBandEnergyShare} is outside 0..1`);
  return line;
}

function readJsonLines(file) {
  const text = readFileSync(file, "utf8");
  if (text.length === 0) return [];
  return text
    .split("\n")
    .filter((line) => line.length > 0)
    .map((line, index) => {
      try {
        return JSON.parse(line);
      } catch {
        throw new Error(`${file}: line ${index + 1} is not JSON`);
      }
    });
}

// Public wave rows carry source, split, label and group; the training stage only needs the label,
// the split and the vector.
export function loadPublic(file) {
  return readJsonLines(file).map((line, index) => {
    const where = `${file}: line ${index + 1}`;
    validateLine(line, where);
    const label = typeof line.label === "string" && line.label.trim().length > 0
      ? line.label.trim().toLowerCase()
      : null;
    return { ...line, label };
  });
}

// Gold rows never carry a path: the CLI keeps paths in a separate local file. A row with a `path`
// key is a mistake in the pipeline, not something to ignore.
export function loadGold(file) {
  return readJsonLines(file).map((line, index) => {
    const where = `${file}: line ${index + 1}`;
    if (Object.prototype.hasOwnProperty.call(line, "path"))
      throw new Error(`${where}: gold row carries a path`);
    validateLine(line, where);
    if (typeof line.recordId !== "string" || line.recordId.length === 0)
      throw new Error(`${where}: missing recordId`);
    if (typeof line.type !== "string") throw new Error(`${where}: missing type`);
    if (!["user", "approved"].includes(line.origin))
      throw new Error(`${where}: origin ${line.origin} is not a confirmation`);
    if (!Array.isArray(line.tags) || line.tags.some((tag) => typeof tag !== "string"))
      throw new Error(`${where}: tags is not a list of names`);
    return {
      ...line,
      type: line.type.trim().toLowerCase(),
      label: line.type.trim().toLowerCase(),
      typeIsCustom: line.typeIsCustom === true,
      tags: [...line.tags],
    };
  });
}

// The band filter of part 7.4 and the label filter of part 7.3: a row whose energy is mostly outside
// the analysis band describes its noise floor, and a custom type is not a label the model can carry.
export function prepare(rows, { minBandShare = 0.1, labels = STANDARD_TYPES } = {}) {
  const allowed = new Set(labels.map((label) => label.toLowerCase()));
  const kept = [];
  for (const row of rows) {
    if (typeof row.inBandEnergyShare === "number" && row.inBandEnergyShare < minBandShare) continue;
    if (row.typeIsCustom === true) continue;
    const label = typeof row.label === "string" ? row.label.trim().toLowerCase() : "";
    if (!allowed.has(label)) continue;
    kept.push({ ...row, label });
  }
  return kept;
}

// Mean and population standard deviation over the selected dimensions, computed on the training
// rows only. A constant dimension (stereo on a mono dataset) is dropped instead of dividing by zero.
export function fitScaler(rows, indices) {
  const kept = [];
  const mu = [];
  const sd = [];
  for (const index of indices) {
    let mean = 0;
    for (const row of rows) mean += row.features[index];
    mean = rows.length === 0 ? 0 : mean / rows.length;
    let variance = 0;
    for (const row of rows) variance += (row.features[index] - mean) ** 2;
    variance = rows.length === 0 ? 0 : variance / rows.length;
    const deviation = Math.sqrt(variance);
    if (!(deviation >= 1e-6)) continue;
    kept.push(index);
    mu.push(mean);
    sd.push(deviation);
  }
  return { indices: kept, mu, sd };
}

export function transform(scaler, values) {
  const out = new Float64Array(scaler.indices.length);
  for (let i = 0; i < scaler.indices.length; i += 1)
    out[i] = (values[scaler.indices[i]] - scaler.mu[i]) / scaler.sd[i];
  return out;
}
