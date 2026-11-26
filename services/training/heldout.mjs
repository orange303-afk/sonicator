// Held-out measurement of the model that ships in resources/ml/global_model.json against contributed
// records. Nothing is trained, tuned or selected here: the model file is read exactly as the app reads
// it, and the contributed rows are the test set. The acceptance rule mirrors AdaptiveClassifier:
// the global score is multiplied by 0.95, a type is published only at or above 0.80, and since the
// local-domain fix a schema-2 query is answered by the shipped model alone when it is loaded
// (`localPolicy: "global-only"`). The earlier merge — a local label wins when the global best is not
// more than 0.02 above it — stays available as "prefer-local" for comparison.
//
// Cold start is the number for a person the model has never met. "app" adds that person's own earlier
// confirmations as local prototypes and merges them the way the app does; it answers "what does the
// library show once someone has used AutoTag for a while". "local" drops the model to show what the
// same 0.80 threshold means on a raw cosine.

import { readFileSync } from "node:fs";
import {
  GLOBAL_PENALTY,
  LOCAL_PRIORITY_MARGIN,
  TYPE_THRESHOLD,
  tagNames,
  validateRecord,
} from "./lib.mjs";
import { STANDARD_TYPES } from "./standard.mjs";
import { predict } from "./v2/models.mjs";

export const DIMENSIONS = 84;
export const FEATURE_SCHEMA_V2 = 2;
export const MODES = ["cold", "app", "local"];
export const CURVE_THRESHOLDS = [0.5, 0.6, 0.7, 0.8, 0.9, 0.95];
// A score is a stated precision, so the calibration table compares the promise with what happened.
export const CALIBRATION_BUCKETS = [0.8, 0.85, 0.9, 0.95];

export const CLASS = Object.freeze({
  model: "model",
  missing: "missing",
  custom: "custom",
  unknown: "unknown",
});

const STANDARD = new Set(STANDARD_TYPES);

function isNumberArray(value, length) {
  return Array.isArray(value) && value.length === length
    && value.every((entry) => typeof entry === "number" && Number.isFinite(entry));
}

// The same document checks GlobalLinearModel::load() performs, so a file the app would refuse is not
// silently measured here either.
export function loadShippedModel(file) {
  const document = JSON.parse(readFileSync(file, "utf8"));
  if (document?.schema !== "model.v1") throw new Error(`${file}: schema is not model.v1`);
  if (document.featureSchema !== FEATURE_SCHEMA_V2) throw new Error(`${file}: featureSchema is not 2`);
  const payload = document.model ?? {};
  if (payload.kind !== "M2")
    throw new Error(`${file}: model kind is ${payload.kind}, this evaluator reads the shipped softmax model`);
  const labels = (payload.labels ?? []).map((label) => String(label).trim().toLowerCase());
  if (labels.length === 0) throw new Error(`${file}: labels are missing`);
  if (new Set(labels).size !== labels.length) throw new Error(`${file}: labels repeat`);

  const indices = document.featureIndices;
  if (!Array.isArray(indices) || indices.length === 0)
    throw new Error(`${file}: featureIndices are missing`);
  if (!isNumberArray(indices, indices.length))
    throw new Error(`${file}: featureIndices are not numbers`);
  if (indices.some((index) => !Number.isInteger(index) || index < 0 || index >= DIMENSIONS))
    throw new Error(`${file}: a feature index is outside 0..${DIMENSIONS - 1}`);

  const dimensions = indices.length;
  const mu = document.mu;
  const sd = document.sd;
  if (!isNumberArray(mu, dimensions)) throw new Error(`${file}: mu does not match featureIndices`);
  if (!isNumberArray(sd, dimensions)) throw new Error(`${file}: sd does not match featureIndices`);
  if (sd.some((value) => !(value >= 1e-6))) throw new Error(`${file}: sd is not positive`);

  const weights = payload.W;
  if (!Array.isArray(weights) || weights.length !== labels.length)
    throw new Error(`${file}: W does not match labels`);
  for (const row of weights)
    if (!isNumberArray(row, dimensions)) throw new Error(`${file}: a W row does not match featureIndices`);
  if (!isNumberArray(payload.b, labels.length)) throw new Error(`${file}: b does not match labels`);
  if (!(typeof payload.temperature === "number" && payload.temperature > 0))
    throw new Error(`${file}: temperature is not positive`);

  const similarity = document.similarityMap ?? [];
  let lastP = -1;
  let lastS = -1;
  for (const pair of similarity) {
    if (!Array.isArray(pair) || pair.length !== 2 || !isNumberArray(pair, 2))
      throw new Error(`${file}: similarityMap pair is malformed`);
    const [probability, score] = pair;
    if (probability < 0 || probability > 1 || score < 0 || score > 1)
      throw new Error(`${file}: similarityMap is outside 0..1`);
    if (probability < lastP - 1e-6 || score < lastS - 1e-6)
      throw new Error(`${file}: similarityMap is not monotone`);
    lastP = probability;
    lastS = score;
  }

  return {
    file,
    generatedAt: typeof document.generatedAt === "string" ? document.generatedAt : null,
    labels,
    labelSet: new Set(labels),
    groups: document.groups ?? {},
    datasets: document.datasets ?? [],
    featureCount: dimensions,
    scaler: { indices: [...indices], mu: [...mu], sd: [...sd] },
    model: {
      kind: "M2",
      labels,
      W: weights,
      b: payload.b,
      temperature: payload.temperature,
      similarityMap: similarity,
    },
  };
}

// One confirmed record becomes one test row. A row's truth is its Type when it has one. A row without
// a type whose tags carry exactly one shipped type name (Kick, Snare, Hat, ...) is used a second,
// weaker way: the person named the sound, just in the tag field instead of the Type field. Rows whose
// tags do not single out one type stay out of the type measurement altogether.
export function readContributedRows(file, { tagLabels = true } = {}) {
  const text = readFileSync(file, "utf8");
  const lines = text.split("\n").filter((line) => line.trim().length > 0);
  const rows = [];
  const stats = {
    lines: lines.length,
    schema1: 0,
    noType: 0,
    tagDerived: 0,
    tagsOnly: 0,
    duplicates: 0,
    conflicts: 0,
    exportByInstall: {},
  };
  const seen = new Map();
  lines.forEach((line, index) => {
    let record;
    try {
      record = JSON.parse(line);
    } catch {
      throw new Error(`${file}: line ${index + 1} is not JSON`);
    }
    validateRecord(record, `${file}: line ${index + 1}`);
    stats.exportByInstall[record.installId] = (stats.exportByInstall[record.installId] ?? 0) + 1;
    if ((record.featureSchema ?? 1) !== FEATURE_SCHEMA_V2) {
      stats.schema1 += 1;
      return;
    }
    const names = tagNames(record);
    const typeLabel = typeof record.type === "string" ? record.type.trim().toLowerCase() : "";
    let label = typeLabel;
    let labelSource = "type";
    let typeIsCustom = record.typeIsCustom === true;
    if (label.length === 0) {
      stats.noType += 1;
      const standardNames = tagLabels
        ? [...new Set(names.map((name) => name.trim().toLowerCase()).filter((name) => STANDARD.has(name)))].sort()
        : [];
      if (standardNames.length !== 1) {
        stats.tagsOnly += 1;
        return;
      }
      label = standardNames[0];
      labelSource = "tag";
      typeIsCustom = false;
      stats.tagDerived += 1;
    }
    const signature = `${label}|${typeIsCustom}|${labelSource}`;
    const existing = seen.get(record.recordId);
    if (existing !== undefined) {
      stats.duplicates += 1;
      if (existing !== signature) stats.conflicts += 1;
      return;
    }
    seen.set(record.recordId, signature);
    rows.push({
      recordId: record.recordId,
      installId: record.installId,
      appVersion: typeof record.appVersion === "string" ? record.appVersion : "",
      sentAt: typeof record.sentAt === "string" ? record.sentAt : "",
      label,
      labelSource,
      typeIsCustom,
      tags: names,
      features: Float64Array.from(record.features),
    });
  });
  stats.exportByInstall = Object.fromEntries(
    Object.entries(stats.exportByInstall).sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : 1)),
  );
  return { rows, stats };
}

// Custom wins over standard: a person who created their own type named it, and the app will never
// publish a standard type in its place from audio alone.
export function classOf(row, labelSet) {
  if (row.typeIsCustom) return CLASS.custom;
  if (labelSet.has(row.label)) return CLASS.model;
  if (STANDARD.has(row.label)) return CLASS.missing;
  return CLASS.unknown;
}

export function unitVector(values, dimensions = DIMENSIONS) {
  let norm = 0;
  for (let index = 0; index < dimensions; index += 1) norm += values[index] * values[index];
  const out = new Float64Array(dimensions);
  const length = Math.sqrt(norm);
  if (!(length > 0)) return out;
  for (let index = 0; index < dimensions; index += 1) out[index] = values[index] / length;
  return out;
}

// Two rows with the same vector are the same audio analysed twice: the catalog often holds the same
// sample under several names. Such a copy is not a second opinion about what the sound is, so the
// local path must not learn from it while the same content is being tested.
export function vectorKey(values, digits = 5) {
  let key = "";
  for (let index = 0; index < values.length; index += 1) key += `${values[index].toFixed(digits)},`;
  return key;
}

function dot(a, b) {
  const n = Math.min(a.length, b.length);
  let sum = 0;
  for (let index = 0; index < n; index += 1) sum += a[index] * b[index];
  return sum;
}

export function buildLocalIndexes(rows) {
  const installs = new Map();
  for (const row of rows) {
    let index = installs.get(row.installId);
    if (index === undefined) {
      index = new Map();
      installs.set(row.installId, index);
    }
    let list = index.get(row.label);
    if (list === undefined) {
      list = [];
      index.set(row.label, list);
    }
    list.push({ recordId: row.recordId, key: vectorKey(row.features), vector: unitVector(row.features) });
  }
  return installs;
}

// Mirrors isPlausibleAdaptiveTag() in AdaptiveClassifier.cpp: a label the features cannot support is
// not allowed to vote, so the evaluation does not credit the local path with a lucky guess the app
// would have filtered out.
export function isPlausibleAdaptiveTag(label, features) {
  if (features.length < 13) return true;
  const normAttack = features[0];
  const normDuration = features[12];
  const lower = label.toLowerCase();
  if (["pad", "pads", "atmos", "atmosphere", "drone", "ambient"].includes(lower)) {
    if (normDuration < 0.025) return false;
    if (normAttack < 0.05 && normDuration < 0.05) return false;
  }
  if (lower === "loop" || lower === "loops") {
    if (normDuration < 0.04) return false;
  }
  return true;
}

// The best cosine per label, with the row under test removed by recordId and every prototype of the
// same content skipped: a copy of the same audio must not stand in as its own second opinion.
export function localScores(query, features, index, skipRecordId) {
  const matches = new Map();
  let twins = 0;
  if (index === undefined) return { matches, twins };
  const queryKey = vectorKey(features);
  for (const [label, list] of index) {
    if (!isPlausibleAdaptiveTag(label, features)) continue;
    let best = 0;
    for (const prototype of list) {
      if (prototype.recordId === skipRecordId) continue;
      if (prototype.key === queryKey) {
        twins += 1;
        continue;
      }
      const score = Math.max(0, Math.min(1, dot(query, prototype.vector)));
      if (score > best) best = score;
    }
    if (best > 0) matches.set(label, best);
  }
  return { matches, twins };
}

function byScore(a, b) {
  if (b.score !== a.score) return b.score - a.score;
  return a.label < b.label ? -1 : a.label > b.label ? 1 : 0;
}

// Global evidence is already mapped to a stated precision by the model's similarityMap, so only the
// app's 0.95 penalty applies here. A local label keeps its raw cosine and wins the tie under
// "prefer-local", which is what the app did until 0.3.3. "global-only" is the shipped rule since
// then: a schema-2 query answered by the loaded model alone.
export function mergeForApp(globalRanked, local, { localPolicy = "prefer-local" } = {}) {
  if (localPolicy === "global-only")
    return globalRanked.map((entry) => ({ ...entry, globalScore: entry.score, localScore: 0, source: "global" }));
  if (local === null || local.size === 0) return globalRanked;
  const byLabel = new Map();
  for (const entry of globalRanked)
    byLabel.set(entry.label, { label: entry.label, score: entry.score, globalScore: entry.score, localScore: 0 });
  for (const [label, score] of local) {
    const entry = byLabel.get(label);
    if (entry === undefined) {
      byLabel.set(label, { label, score, globalScore: 0, localScore: score });
      continue;
    }
    entry.localScore = score;
    if (score >= entry.globalScore) entry.score = score;
  }
  const entries = [...byLabel.values()]
    .map((entry) => ({ ...entry, source: entry.localScore > 0 && entry.localScore >= entry.globalScore ? "local" : "global" }))
    .sort(byScore);
  if (entries.length > 1 && entries[0].source !== "local") {
    let bestLocal = -1;
    for (let index = 0; index < entries.length; index += 1) {
      if (entries[index].localScore <= 0) continue;
      if (bestLocal < 0 || entries[index].localScore > entries[bestLocal].localScore) bestLocal = index;
    }
    if (bestLocal >= 0 && entries[0].score - entries[bestLocal].localScore <= LOCAL_PRIORITY_MARGIN + 1e-6) {
      const [promoted] = entries.splice(bestLocal, 1);
      promoted.score = promoted.localScore;
      promoted.source = "local";
      entries.unshift(promoted);
    }
  }
  return entries;
}

export function wilson(successes, total) {
  if (total === 0) return { low: 0, high: 0 };
  const z = 1.96;
  const p = successes / total;
  const denominator = 1 + (z * z) / total;
  const centre = p + (z * z) / (2 * total);
  const spread = z * Math.sqrt((p * (1 - p)) / total + (z * z) / (4 * total * total));
  return {
    low: Math.max(0, (centre - spread) / denominator),
    high: Math.min(1, (centre + spread) / denominator),
  };
}

function blankBucket() {
  return {
    records: 0,
    correct: 0,
    top3Correct: 0,
    accepted: 0,
    acceptedCorrect: 0,
    scoreSum: 0,
    samples: [],
    wrong: new Map(),
  };
}

function addOutcome(bucket, outcome) {
  bucket.records += 1;
  bucket.scoreSum += outcome.score;
  bucket.samples.push({ score: outcome.score, hit: outcome.hit ? 1 : 0 });
  if (outcome.hit) bucket.correct += 1;
  if (outcome.top3) bucket.top3Correct += 1;
  if (!outcome.accepted) return;
  bucket.accepted += 1;
  if (outcome.hit) bucket.acceptedCorrect += 1;
  else if (outcome.predicted !== null) bucket.wrong.set(outcome.predicted, (bucket.wrong.get(outcome.predicted) ?? 0) + 1);
}

function sortedCounts(map) {
  return [...map.entries()]
    .map(([label, count]) => ({ label, count }))
    .sort((a, b) => b.count - a.count || (a.label < b.label ? -1 : a.label > b.label ? 1 : 0));
}

export function finalize(bucket) {
  const records = bucket.records;
  const accepted = bucket.accepted;
  return {
    records,
    correct: bucket.correct,
    top1: records === 0 ? 0 : bucket.correct / records,
    top3: records === 0 ? 0 : bucket.top3Correct / records,
    accepted,
    acceptedCorrect: bucket.acceptedCorrect,
    coverage: records === 0 ? 0 : accepted / records,
    precision: accepted === 0 ? 0 : bucket.acceptedCorrect / accepted,
    precisionInterval: wilson(bucket.acceptedCorrect, accepted),
    top1Interval: wilson(bucket.correct, records),
    meanScore: records === 0 ? 0 : bucket.scoreSum / records,
    curves: CURVE_THRESHOLDS.map((threshold) => {
      const hits = bucket.samples.filter((sample) => sample.score >= threshold);
      return {
        threshold,
        coverage: records === 0 ? 0 : hits.length / records,
        precision: hits.length === 0 ? 0 : hits.filter((sample) => sample.hit === 1).length / hits.length,
      };
    }),
    wrong: sortedCounts(bucket.wrong).slice(0, 8),
  };
}

function bucketKey(score) {
  let key = CALIBRATION_BUCKETS[0];
  for (const edge of CALIBRATION_BUCKETS) if (score >= edge) key = edge;
  return key;
}

export function evaluateContributed({
  rows,
  prototypes = null,
  shipped,
  threshold = TYPE_THRESHOLD,
  localPolicy = "prefer-local",
}) {
  const labelSet = shipped.labelSet;
  const indexed = rows.map((row) => ({ ...row, class: classOf(row, labelSet), unit: unitVector(row.features) }));
  // The local path only ever holds rows whose Type a person set: a tag is not a type prototype in the
  // app, so a tag-derived test set is scored against the typed rows of the same install.
  const indexes = buildLocalIndexes(prototypes === null ? indexed : prototypes);

  const named = Object.fromEntries(MODES.map((mode) => [mode, blankBucket()]));
  const standard = Object.fromEntries(MODES.map((mode) => [mode, blankBucket()]));
  const installs = new Map();
  const labels = new Map();
  const missing = new Map();
  const custom = new Map();
  const unknown = new Map();
  const calibration = new Map(CALIBRATION_BUCKETS.map((edge) => [edge, { records: 0, correct: 0, scoreSum: 0 }]));
  const confidentWrong = Object.fromEntries(MODES.map((mode) => [mode, new Map()]));
  const localEvidence = { recordsWithLocal: 0, localOnly: 0, globalOnly: 0, twins: 0 };
  const localBest = [];
  const classes = { model: 0, missing: 0, custom: 0, unknown: 0 };
  const sources = { type: 0, tag: 0 };

  const namedEntry = (map, key, seed) => {
    let entry = map.get(key);
    if (entry === undefined) {
      entry = { key, records: 0, installs: new Set(), modes: Object.fromEntries(MODES.map((mode) => [mode, blankBucket()])), topCold: new Map(), ...seed };
      map.set(key, entry);
    }
    return entry;
  };

  for (const row of indexed) {
    const truth = row.label;
    const kind = row.class;
    classes[kind] += 1;
    if (row.labelSource === "tag") sources.tag += 1;
    else sources.type += 1;
    const raw = predict(shipped.model, shipped.scaler, row.features);
    const globalRanked = raw.ranked.map((entry) => ({
      label: entry.label,
      score: entry.score * GLOBAL_PENALTY,
      probability: entry.probability,
    }));
    const { matches: local, twins } = localScores(row.unit, row.features, indexes.get(row.installId), row.recordId);
    const appRanked = mergeForApp(globalRanked, local, { localPolicy });
    const localRanked = [...local.entries()]
      .map(([label, score]) => ({ label, score, source: "local" }))
      .sort(byScore);
    const rankings = { cold: globalRanked, app: appRanked, local: localRanked };

    let install = installs.get(row.installId);
    if (install === undefined) {
      install = {
        installId: row.installId,
        versions: new Set(),
        records: { all: 0, model: 0, missing: 0, custom: 0, unknown: 0 },
        named: Object.fromEntries(MODES.map((mode) => [mode, blankBucket()])),
        standard: Object.fromEntries(MODES.map((mode) => [mode, blankBucket()])),
        localEvidence: 0,
        appLocalWins: 0,
        appGlobalWins: 0,
        appLocalPublished: 0,
        appLocalCorrect: 0,
        twins: 0,
      };
      installs.set(row.installId, install);
    }
    install.twins += twins;
    localEvidence.twins += twins;
    install.records.all += 1;
    install.records[kind] = (install.records[kind] ?? 0) + 1;
    if (row.appVersion.length > 0) install.versions.add(row.appVersion);
    if (local.size > 0) {
      install.localEvidence += 1;
      localEvidence.recordsWithLocal += 1;
      let best = 0;
      for (const score of local.values()) best = Math.max(best, score);
      localBest.push(best);
    }

    if (kind === CLASS.model) {
      const entry = namedEntry(labels, truth, {});
      entry.records += 1;
      entry.installs.add(row.installId);
    } else if (kind === CLASS.missing) {
      const entry = namedEntry(missing, truth, {});
      entry.records += 1;
      entry.installs.add(row.installId);
      const top = globalRanked[0];
      if (top !== undefined) entry.topCold.set(top.label, (entry.topCold.get(top.label) ?? 0) + 1);
    } else if (kind === CLASS.custom) {
      const entry = namedEntry(custom, truth, {});
      entry.records += 1;
      entry.installs.add(row.installId);
      const top = globalRanked[0];
      if (top !== undefined) entry.topCold.set(top.label, (entry.topCold.get(top.label) ?? 0) + 1);
    } else {
      const entry = namedEntry(unknown, truth, {});
      entry.records += 1;
      entry.installs.add(row.installId);
      const top = globalRanked[0];
      if (top !== undefined) entry.topCold.set(top.label, (entry.topCold.get(top.label) ?? 0) + 1);
    }

    for (const mode of MODES) {
      const ranked = rankings[mode];
      const top = ranked.length > 0 ? ranked[0] : null;
      const hit = top !== null && top.label === truth;
      const outcome = {
        score: top === null ? 0 : top.score,
        hit,
        top3: ranked.slice(0, 3).some((entry) => entry.label === truth),
        accepted: top !== null && top.score >= threshold,
        predicted: top === null ? null : top.label,
      };
      if (kind === CLASS.model) {
        addOutcome(named[mode], outcome);
        addOutcome(standard[mode], outcome);
        addOutcome(labels.get(truth).modes[mode], outcome);
        addOutcome(install.named[mode], outcome);
        addOutcome(install.standard[mode], outcome);
        if (mode === "cold" && outcome.accepted) {
          const bucket = calibration.get(bucketKey(outcome.score));
          bucket.records += 1;
          bucket.scoreSum += outcome.score;
          if (hit) bucket.correct += 1;
        }
        if (outcome.accepted && !hit)
          confidentWrong[mode].set(`${truth}\u0000${top.label}`, (confidentWrong[mode].get(`${truth}\u0000${top.label}`) ?? 0) + 1);
      } else if (kind === CLASS.missing) {
        addOutcome(standard[mode], outcome);
        addOutcome(missing.get(truth).modes[mode], outcome);
        addOutcome(install.standard[mode], outcome);
      } else if (kind === CLASS.custom) {
        addOutcome(custom.get(truth).modes[mode], outcome);
      } else {
        addOutcome(unknown.get(truth).modes[mode], outcome);
      }
      if (mode === "app" && top !== null) {
        if (top.source === "local") {
          install.appLocalWins += 1;
          if (outcome.accepted) {
            install.appLocalPublished += 1;
            if (hit) install.appLocalCorrect += 1;
          }
        } else {
          install.appGlobalWins += 1;
        }
      }
    }
    if (appRanked.length > 0) {
      if (appRanked[0].source === "local") localEvidence.localOnly += 1;
      else localEvidence.globalOnly += 1;
    }
  }

  const finishInstall = (install) => ({
    installId: install.installId,
    versions: [...install.versions].sort(),
    records: install.records,
    named: Object.fromEntries(MODES.map((mode) => [mode, finalize(install.named[mode])])),
    standard: Object.fromEntries(MODES.map((mode) => [mode, finalize(install.standard[mode])])),
    localEvidence: install.localEvidence,
    appLocalWins: install.appLocalWins,
    appGlobalWins: install.appGlobalWins,
    appLocalPublished: install.appLocalPublished,
    appLocalCorrect: install.appLocalCorrect,
    twins: install.twins,
  });

  const finishLabel = (entry, withTop) => ({
    label: entry.key,
    records: entry.records,
    installs: [...entry.installs].sort(),
    modes: Object.fromEntries(MODES.map((mode) => [mode, finalize(entry.modes[mode])])),
    ...(withTop ? { topCold: sortedCounts(entry.topCold) } : {}),
  });

  const samples = localBest.sort((a, b) => a - b);
  const median = samples.length === 0 ? 0 : samples[Math.floor(samples.length / 2)];

  return {
    threshold,
    localPolicy,
    generatedAt: shipped.generatedAt,
    modelFile: shipped.file,
    labels: shipped.labels,
    featureCount: shipped.featureCount,
    modes: MODES,
    dataset: { records: indexed.length, classes, sources },
    named: Object.fromEntries(MODES.map((mode) => [mode, finalize(named[mode])])),
    standard: Object.fromEntries(MODES.map((mode) => [mode, finalize(standard[mode])])),
    installs: [...installs.values()].map(finishInstall)
      .sort((a, b) => b.records.all - a.records.all || (a.installId < b.installId ? -1 : 1)),
    perLabel: [...labels.values()].map((entry) => finishLabel(entry, false))
      .sort((a, b) => b.records - a.records || (a.label < b.label ? -1 : 1)),
    missing: [...missing.values()].map((entry) => finishLabel(entry, true))
      .sort((a, b) => b.records - a.records || (a.label < b.label ? -1 : 1)),
    custom: [...custom.values()].map((entry) => finishLabel(entry, true))
      .sort((a, b) => b.records - a.records || (a.label < b.label ? -1 : 1)),
    unknown: [...unknown.values()].map((entry) => finishLabel(entry, true))
      .sort((a, b) => b.records - a.records || (a.label < b.label ? -1 : 1)),
    calibration: CALIBRATION_BUCKETS.map((edge) => {
      const bucket = calibration.get(edge);
      return {
        from: edge,
        to: edge === CALIBRATION_BUCKETS.at(-1) ? 1 : edge + 0.05,
        records: bucket.records,
        precision: bucket.records === 0 ? 0 : bucket.correct / bucket.records,
        meanScore: bucket.records === 0 ? 0 : bucket.scoreSum / bucket.records,
      };
    }),
    confidentWrong: Object.fromEntries(MODES.map((mode) => [
      mode,
      sortedCounts(confidentWrong[mode]).slice(0, 12).map((entry) => {
        const [truth, predicted] = entry.label.split("\u0000");
        return { truth, predicted, count: entry.count };
      }),
    ])),
    localEvidence: {
      ...localEvidence,
      medianBest: median,
      records: indexed.length,
    },
  };
}
