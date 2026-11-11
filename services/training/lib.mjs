// Shared model code for the training stage. Kept dependency-free so a checkout can rebuild the
// shipped prototypes with nothing but Node.

export const FEATURE_SCHEMA = 1;
export const FEATURE_COUNT = 14;
export const PROTOTYPE_SCHEMA = "prototypes.v1";
export const MIN_INSTALLS = 3;
export const MIN_SUPPORT = 10;
export const MAX_PROTOTYPES_PER_LABEL = 8;
export const MAX_LABELS_PER_KIND = 200;
// A guard against one enormous label, not a weighting rule: every install carries the same total
// weight inside a label, so a big library cannot fill every prototype with its own material.
export const MAX_ROWS_PER_INSTALL_PER_LABEL = 2000;
export const CLUSTER_ITERATIONS = 30;
export const QUANTIZE_STEP = 1000;
// Global evidence is worth slightly less than a person's own confirmation.
export const GLOBAL_PENALTY = 0.95;
export const LOCAL_PRIORITY_MARGIN = 0.02;
export const TYPE_THRESHOLD = 0.8;
export const TAG_THRESHOLD = 0.85;

export function quantize(value) {
  if (!Number.isFinite(value)) return 0;
  return Math.min(1, Math.max(0, Math.round(value * QUANTIZE_STEP) / QUANTIZE_STEP));
}

export function parseDataset(text) {
  return text
    .split("\n")
    .map((line) => line.trim())
    .filter((line) => line.length > 0)
    .map((line, index) => {
      let record;
      try {
        record = JSON.parse(line);
      } catch {
        throw new Error(`line ${index + 1} is not JSON`);
      }
      validateRecord(record, `line ${index + 1}`);
      return record;
    });
}

export function validateRecord(record, where = "record") {
  if (typeof record.recordId !== "string" || record.recordId.length === 0)
    throw new Error(`${where}: missing recordId`);
  if (typeof record.installId !== "string" || record.installId.length === 0)
    throw new Error(`${where}: missing installId`);
  if (record.featureSchema !== FEATURE_SCHEMA)
    throw new Error(`${where}: featureSchema ${record.featureSchema} is not ${FEATURE_SCHEMA}`);
  if (!Array.isArray(record.features) || record.features.length !== FEATURE_COUNT)
    throw new Error(`${where}: expected ${FEATURE_COUNT} features`);
  for (const value of record.features)
    if (typeof value !== "number" || !Number.isFinite(value) || value < 0 || value > 1)
      throw new Error(`${where}: feature ${value} is outside 0..1`);
  if (record.type !== undefined && (typeof record.type !== "string" || record.type.trim().length === 0))
    throw new Error(`${where}: empty type`);
  if (record.tags !== undefined && !Array.isArray(record.tags))
    throw new Error(`${where}: tags is not an array`);
  if (record.type === undefined && (record.tags ?? []).length === 0)
    throw new Error(`${where}: no type and no tags`);
}

export function compareText(a, b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

export function normalizeRecords(records) {
  const byId = new Map();
  for (const record of [...records].sort((a, b) => compareText(a.recordId, b.recordId)))
    if (!byId.has(record.recordId)) byId.set(record.recordId, record);
  return [...byId.values()];
}

export function tagNames(record) {
  return (record.tags ?? [])
    .map((tag) => (typeof tag === "string" ? tag : tag?.name))
    .filter((name) => typeof name === "string" && name.trim().length > 0);
}

export function cosine(a, b) {
  if (a.length === 0 || b.length === 0) return 0;
  let dot = 0;
  let normA = 0;
  let normB = 0;
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i += 1) {
    dot += a[i] * b[i];
    normA += a[i] * a[i];
    normB += b[i] * b[i];
  }
  if (normA <= 0 || normB <= 0) return 0;
  return Math.max(0, Math.min(1, dot / (Math.sqrt(normA) * Math.sqrt(normB))));
}

export function distance(a, b) {
  return 1 - cosine(a, b);
}

// One entry per label with every record that carries it, sorted by recordId.
export function collectLabels(records, { includeCustomTypes = false } = {}) {
  const labels = new Map();
  for (const record of normalizeRecords(records)) {
    const typeAllowed = record.type && (includeCustomTypes || !record.typeIsCustom);
    const entries = [
      ...(typeAllowed ? [{ kind: "type", name: record.type }] : []),
      ...tagNames(record).map((name) => ({ kind: "tag", name })),
    ];
    const seen = new Set();
    for (const entry of entries) {
      const key = `${entry.kind}:${entry.name.toLowerCase()}`;
      if (seen.has(key)) continue; // one record confirms a label once
      seen.add(key);
      const label = labels.get(key) ?? {
        key,
        label: entry.name,
        kind: entry.kind,
        rows: [],
        installs: new Set(),
      };
      label.rows.push(record);
      label.installs.add(record.installId);
      labels.set(key, label);
    }
  }
  return [...labels.values()].map((label) => ({
    ...label,
    support: label.rows.length,
    installSupport: label.installs.size,
  }));
}

function capRows(rows) {
  const byInstall = new Map();
  const kept = [];
  for (const row of rows) {
    const count = byInstall.get(row.installId) ?? 0;
    if (count >= MAX_ROWS_PER_INSTALL_PER_LABEL) continue;
    byInstall.set(row.installId, count + 1);
    kept.push(row);
  }
  return kept;
}

// Each install carries the same total weight inside a label: a row of a ten-record install weighs
// as much as the combined rows of a thousand-record one. With a single install every weight is
// equal, so the result reduces to a plain mean.
function installWeights(rows) {
  const counts = new Map();
  for (const row of rows) counts.set(row.installId, (counts.get(row.installId) ?? 0) + 1);
  return rows.map((row) => 1 / counts.get(row.installId));
}

function dot(a, b) {
  let sum = 0;
  const size = Math.min(a.length, b.length);
  for (let i = 0; i < size; i += 1) sum += a[i] * b[i];
  return sum;
}

function unit(vector) {
  let sum = 0;
  for (const value of vector) sum += value * value;
  const norm = Math.sqrt(sum);
  return norm > 0 ? vector.map((value) => value / norm) : vector.slice();
}

function weightedMean(rows, weights) {
  const size = rows[0].features.length;
  const mean = new Array(size).fill(0);
  let total = 0;
  for (let i = 0; i < rows.length; i += 1) {
    total += weights[i];
    for (let j = 0; j < size; j += 1) mean[j] += rows[i].features[j] * weights[i];
  }
  return total > 0 ? mean.map((value) => value / total) : mean;
}

function sameVector(a, b, tolerance = 1e-6) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i += 1) if (Math.abs(a[i] - b[i]) > tolerance) return false;
  return true;
}

// Weighted k-means over the label's records, with the cluster centres returned as prototypes.
// Farthest-first start, then each centre moves onto the weighted mean of its members, so a rare
// sample may sit alone in a cluster without becoming the centre of the label. Deterministic:
// rows arrive in recordId order, ties go to the lower index, and the centres are ordered by their
// first member.
export function selectPrototypes(rows, max = MAX_PROTOTYPES_PER_LABEL) {
  const points = capRows(rows);
  if (points.length === 0) return [];
  const weights = installWeights(points);
  const k = Math.max(1, Math.min(max, points.length));
  if (k === 1) return [weightedMean(points, weights)];

  const units = points.map((row) => unit(row.features));
  const chosen = [0];
  while (chosen.length < k) {
    let best = -1;
    let bestDistance = -1;
    for (let i = 0; i < points.length; i += 1) {
      if (chosen.includes(i)) continue;
      let nearest = Infinity;
      for (const j of chosen) nearest = Math.min(nearest, 1 - dot(units[i], units[j]));
      if (nearest > bestDistance) {
        bestDistance = nearest;
        best = i;
      }
    }
    if (best < 0) break;
    chosen.push(best);
  }

  let centres = chosen.map((index) => units[index]);
  let assignments = points.map(() => -1);
  for (let iteration = 0; iteration < CLUSTER_ITERATIONS; iteration += 1) {
    let changed = false;
    assignments = units.map((vector, index) => {
      let bestCentre = 0;
      let bestScore = -Infinity;
      for (let c = 0; c < centres.length; c += 1) {
        const score = dot(vector, centres[c]);
        if (score > bestScore + 1e-12) {
          bestScore = score;
          bestCentre = c;
        }
      }
      if (assignments[index] !== bestCentre) changed = true;
      return bestCentre;
    });
    centres = centres.map((old, c) => {
      const members = points.filter((_, index) => assignments[index] === c);
      if (members.length === 0) return old;
      return unit(weightedMean(members, weights.filter((_, index) => assignments[index] === c)));
    });
    if (!changed) break;
  }

  const prototypes = [];
  for (let c = 0; c < centres.length; c += 1) {
    const members = points.filter((_, index) => assignments[index] === c);
    if (members.length === 0) continue;
    prototypes.push(weightedMean(members, weights.filter((_, index) => assignments[index] === c)));
  }

  const unique = [];
  for (const vector of prototypes) {
    if (unique.some((kept) => sameVector(kept, vector))) continue;
    unique.push(vector);
  }
  return unique;
}

export function buildPrototypes(records, options = {}) {
  const normalized = normalizeRecords(records);
  const installs = new Set(normalized.map((record) => record.installId));
  const minInstalls = options.minInstalls ?? MIN_INSTALLS;
  const minSupport = options.minSupport ?? MIN_SUPPORT;
  const generatedAt = options.generatedAt ?? defaultGeneratedAt(normalized);

  const labels = collectLabels(normalized, { includeCustomTypes: options.includeCustomTypes ?? false })
    .filter((label) => label.installSupport >= minInstalls && label.support >= minSupport)
    .map((label) => ({
      label: label.label,
      kind: label.kind,
      support: label.support,
      installSupport: label.installSupport,
      prototypes: selectPrototypes(label.rows).map((features) => features.map(quantize)),
    }));

  const byKind = new Map();
  for (const label of labels) {
    const list = byKind.get(label.kind) ?? [];
    list.push(label);
    byKind.set(label.kind, list);
  }
  const capped = [];
  for (const kind of ["type", "tag"]) {
    const list = (byKind.get(kind) ?? []).sort((a, b) =>
      b.installSupport - a.installSupport
      || b.support - a.support
      || compareText(a.label, b.label));
    capped.push(...list.slice(0, options.maxLabelsPerKind ?? MAX_LABELS_PER_KIND));
  }
  capped.sort((a, b) => compareText(a.kind, b.kind) || compareText(a.label, b.label));

  return {
    schema: PROTOTYPE_SCHEMA,
    featureSchema: FEATURE_SCHEMA,
    generatedAt,
    records: normalized.length,
    installs: installs.size,
    labels: capped,
  };
}

function defaultGeneratedAt(records) {
  let latest = 0;
  for (const record of records)
    if (typeof record.updatedAt === "number" && record.updatedAt > latest) latest = record.updatedAt;
  if (latest > 0) return new Date(latest).toISOString();
  // The export does not carry updatedAt; sentAt identifies when the record was last received,
  // which is deterministic for a given dataset and still meaningful in the report.
  let latestSent = "";
  for (const record of records)
    if (typeof record.sentAt === "string" && record.sentAt > latestSent) latestSent = record.sentAt;
  if (latestSent !== "") {
    const parsed = new Date(latestSent);
    if (!Number.isNaN(parsed.getTime())) return parsed.toISOString();
  }
  return "1970-01-01T00:00:00.000Z";
}

export function serializePrototypes(file) {
  return `${JSON.stringify(file, null, 2)}\n`;
}

// Turns a prototype file (or a freshly built one) into the shape prediction uses.
export function indexPrototypes(file) {
  const index = new Map([["type", new Map()], ["tag", new Map()]]);
  for (const label of file.labels ?? []) {
    const key = String(label.label).toLowerCase();
    const target = index.get(label.kind);
    if (!target) continue;
    target.set(key, {
      label: label.label,
      kind: label.kind,
      prototypes: (label.prototypes ?? []).map((vector) => vector.map(Number)),
    });
  }
  return index;
}

// The local side of the app keeps every confirmed row as a prototype, without clustering, so the
// evaluation builds its local index the same way. Vectors are kept by reference, which lets a
// caller drop the record under test by identity instead of by value.
export function indexRecords(records) {
  const index = new Map([["type", new Map()], ["tag", new Map()]]);
  const add = (kind, name, features) => {
    const target = index.get(kind);
    const key = String(name).toLowerCase();
    const entry = target.get(key) ?? { label: String(name), kind, prototypes: [] };
    entry.prototypes.push(features);
    target.set(key, entry);
  };
  for (const record of records) {
    if (record.type) add("type", record.type, record.features);
    for (const tag of tagNames(record)) add("tag", tag, record.features);
  }
  return index;
}

function bestScore(features, labelIndex, penalty) {
  let best = null;
  for (const entry of labelIndex.values()) {
    let score = 0;
    for (const prototype of entry.prototypes)
      score = Math.max(score, cosine(features, prototype) * penalty);
    if (score > 0 && (best === null || score > best.score))
      best = { label: entry.label, score, source: penalty < 1 ? "global_adaptive" : "user_adaptive" };
  }
  return best;
}

// Mirrors the app: the global score is penalised before the acceptance threshold, and a local
// label wins when the global best beats it by no more than LOCAL_PRIORITY_MARGIN.
function preferLocal(local, global) {
  if (local === null) return global;
  if (global === null) return local;
  if (local.label.toLowerCase() === global.label.toLowerCase())
    return global.score > local.score ? { ...global, source: "global_adaptive" } : local;
  if (global.score - local.score <= LOCAL_PRIORITY_MARGIN + 1e-12) return local;
  return global;
}

export function predictType(features, localIndex, globalIndex) {
  const local = bestScore(features, localIndex.get("type") ?? new Map(), 1);
  const global = bestScore(features, globalIndex.get("type") ?? new Map(), GLOBAL_PENALTY);
  const winner = global === null ? local : preferLocal(local, global);
  if (winner === null || winner.score < TYPE_THRESHOLD) return null;
  return winner;
}

export function predictTags(features, localIndex, globalIndex, topK = 3) {
  const collect = (labelIndex, penalty, source) => {
    const entries = [];
    for (const entry of labelIndex.values()) {
      let score = 0;
      for (const prototype of entry.prototypes)
        score = Math.max(score, cosine(features, prototype) * penalty);
      if (score > 0) entries.push({ label: entry.label, score, source });
    }
    return entries.sort((a, b) => b.score - a.score || a.label.localeCompare(b.label));
  };
  const local = collect(localIndex.get("tag") ?? new Map(), 1, "user_adaptive");
  const global = collect(globalIndex.get("tag") ?? new Map(), GLOBAL_PENALTY, "global_adaptive");
  const merged = [...local, ...global].sort((a, b) => {
    const aScore = a.score + (a.source === "user_adaptive" ? LOCAL_PRIORITY_MARGIN : 0);
    const bScore = b.score + (b.source === "user_adaptive" ? LOCAL_PRIORITY_MARGIN : 0);
    return bScore - aScore || a.label.localeCompare(b.label);
  });
  const seen = new Set();
  const out = [];
  for (const entry of merged) {
    const key = entry.label.toLowerCase();
    if (seen.has(key)) continue;
    seen.add(key);
    if (entry.score < TAG_THRESHOLD) continue;
    out.push(entry);
    if (out.length >= topK) break;
  }
  return out;
}

export function buildIndex(records, { minInstalls = MIN_INSTALLS, minSupport = MIN_SUPPORT, includeCustomTypes = false } = {}) {
  return indexPrototypes(buildPrototypes(records, { minInstalls, minSupport, includeCustomTypes }));
}
