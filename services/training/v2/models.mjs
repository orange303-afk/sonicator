// The two candidate models of part 7.4. Both see standardised schema-2 vectors, both are
// deterministic (no randomness anywhere), and both return a similar-shaped answer so the report can
// compare them on the same gold rows.
//
// M1: weighted k-means centroids, the same algorithm the shipped schema-1 prototypes use.
// M2: a linear layer trained with full-batch Adam, class weights and L2, then calibrated with a
// temperature on the validation split.

import { cosine, selectPrototypes } from "../lib.mjs";
import { applyMap, isotonicMap } from "./calibration.mjs";
import { transform } from "./features.mjs";

const EPSILON = 1e-12;

function sortedLabels(rows) {
  return [...new Set(rows.map((row) => row.label))].sort();
}

function transformed(rows, scaler) {
  return rows.map((row) => transform(scaler, row.features));
}

export function trainCentroids(rows, scaler, { maxPerLabel = 8, sourceKey = "source" } = {}) {
  const labels = sortedLabels(rows);
  const prototypes = [];
  for (const label of labels) {
    const members = rows
      .filter((row) => row.label === label)
      .sort((a, b) => (String(a.recordId ?? "").localeCompare(String(b.recordId ?? ""))));
    const points = members.map((row) => ({
      installId: String(row[sourceKey] ?? "public"),
      features: Array.from(transform(scaler, row.features)),
    }));
    prototypes.push({ label, prototypes: selectPrototypes(points, maxPerLabel) });
  }
  return { kind: "M1", labels, prototypes };
}

function softmax(logits, temperature) {
  const scaled = logits.map((value) => value / temperature);
  let max = -Infinity;
  for (const value of scaled) max = Math.max(max, value);
  const exps = scaled.map((value) => Math.exp(value - max));
  let total = 0;
  for (const value of exps) total += value;
  return exps.map((value) => value / total);
}

function accuracyOf({ W, b, labels }, X, y, temperature = 1) {
  let correct = 0;
  for (let i = 0; i < y.length; i += 1) {
    const logits = new Array(labels.length).fill(0);
    for (let c = 0; c < labels.length; c += 1) {
      let sum = b[c];
      for (let j = 0; j < X[i].length; j += 1) sum += W[c][j] * X[i][j];
      logits[c] = sum;
    }
    let best = 0;
    for (let c = 1; c < logits.length; c += 1) if (logits[c] > logits[best]) best = c;
    if (best === y[i]) correct += 1;
  }
  return y.length === 0 ? 0 : correct / y.length;
}

function fitTemperature(W, b, labels, X, y) {
  let bestTemperature = 1;
  let bestLoss = Infinity;
  for (let step = 5; step <= 60; step += 1) {
    const temperature = step / 20; // 0.25 .. 3.00
    let loss = 0;
    for (let i = 0; i < y.length; i += 1) {
      const logits = new Array(labels.length).fill(0);
      for (let c = 0; c < labels.length; c += 1) {
        let sum = b[c];
        for (let j = 0; j < X[i].length; j += 1) sum += W[c][j] * X[i][j];
        logits[c] = sum;
      }
      const probabilities = softmax(logits, temperature);
      loss -= Math.log(Math.max(probabilities[y[i]], 1e-12));
    }
    if (loss < bestLoss - 1e-12) {
      bestLoss = loss;
      bestTemperature = temperature;
    }
  }
  return Math.round(bestTemperature * 1000) / 1000;
}

export function trainSoftmax(train, val, scaler, { l2 = 1e-3, epochs = 300, patience = 20, lr = 0.05 } = {}) {
  const labels = sortedLabels(train);
  if (labels.length < 2) throw new Error("softmax needs at least two labels");
  const dimensions = scaler.indices.length;
  const Xtrain = transformed(train, scaler);
  const Xval = transformed(val, scaler);
  const yTrain = train.map((row) => labels.indexOf(row.label));
  const yVal = val.map((row) => labels.indexOf(row.label));

  const counts = new Array(labels.length).fill(0);
  for (const index of yTrain) counts[index] += 1;
  const maxCount = Math.max(...counts);
  const classWeights = counts.map((count) => Math.min(10, maxCount / Math.max(1, count)));

  const W = labels.map(() => new Array(dimensions).fill(0));
  const b = new Array(labels.length).fill(0);
  const mW = labels.map(() => new Array(dimensions).fill(0));
  const vW = labels.map(() => new Array(dimensions).fill(0));
  const mB = new Array(labels.length).fill(0);
  const vB = new Array(labels.length).fill(0);
  const beta1 = 0.9, beta2 = 0.999, eps = 1e-8;

  let best = { W: W.map((row) => row.slice()), b: b.slice() };
  let bestScore = -Infinity;
  let waited = 0;

  for (let epoch = 1; epoch <= epochs; epoch += 1) {
    const gradW = labels.map(() => new Array(dimensions).fill(0));
    const gradB = new Array(labels.length).fill(0);

    for (let i = 0; i < yTrain.length; i += 1) {
      const logits = new Array(labels.length).fill(0);
      for (let c = 0; c < labels.length; c += 1) {
        let sum = b[c];
        for (let j = 0; j < dimensions; j += 1) sum += W[c][j] * Xtrain[i][j];
        logits[c] = sum;
      }
      const probabilities = softmax(logits, 1);
      const weight = classWeights[yTrain[i]] / yTrain.length;
      for (let c = 0; c < labels.length; c += 1) {
        const delta = weight * (probabilities[c] - (c === yTrain[i] ? 1 : 0));
        gradB[c] += delta;
        for (let j = 0; j < dimensions; j += 1) gradW[c][j] += delta * Xtrain[i][j];
      }
    }

    for (let c = 0; c < labels.length; c += 1)
      for (let j = 0; j < dimensions; j += 1) gradW[c][j] += l2 * W[c][j];

    const correction1 = 1 - Math.pow(beta1, epoch);
    const correction2 = 1 - Math.pow(beta2, epoch);
    for (let c = 0; c < labels.length; c += 1) {
      for (let j = 0; j < dimensions; j += 1) {
        mW[c][j] = beta1 * mW[c][j] + (1 - beta1) * gradW[c][j];
        vW[c][j] = beta2 * vW[c][j] + (1 - beta2) * gradW[c][j] * gradW[c][j];
        W[c][j] -= lr * (mW[c][j] / correction1) / (Math.sqrt(vW[c][j] / correction2) + eps);
      }
      mB[c] = beta1 * mB[c] + (1 - beta1) * gradB[c];
      vB[c] = beta2 * vB[c] + (1 - beta2) * gradB[c] * gradB[c];
      b[c] -= lr * (mB[c] / correction1) / (Math.sqrt(vB[c] / correction2) + eps);
    }

    const score = Xval.length > 0
      ? accuracyOf({ W, b, labels }, Xval, yVal)
      : accuracyOf({ W, b, labels }, Xtrain, yTrain);
    if (score > bestScore + EPSILON) {
      bestScore = score;
      best = { W: W.map((row) => row.slice()), b: b.slice() };
      waited = 0;
    } else {
      waited += 1;
      if (waited >= patience) break;
    }
  }

  const temperature = Xval.length > 0 ? fitTemperature(best.W, best.b, labels, Xval, yVal) : 1;
  return { kind: "M2", labels, W: best.W, b: best.b, temperature };
}

function mapProbability(model, probability) {
  const map = model.similarityMap;
  if (!Array.isArray(map) || map.length === 0) return probability;
  return toSimilarity(map, probability);
}

function logitsOf(model, vector) {
  return model.labels.map((_, c) => {
    let sum = model.b[c];
    for (let j = 0; j < vector.length; j += 1) sum += model.W[c][j] * vector[j];
    return sum;
  });
}

export function predict(model, scaler, features) {
  const vector = transform(scaler, features);
  if (model.kind === "M1") {
    const entries = [];
    for (const label of model.prototypes) {
      let best = 0;
      for (const prototype of label.prototypes) best = Math.max(best, cosine(vector, prototype));
      entries.push({ label: label.label, score: best });
    }
    entries.sort((a, b) => b.score - a.score || a.label.localeCompare(b.label));
    if (entries.length === 0) return { label: null, score: 0, ranked: [] };
    return { label: entries[0].label, score: entries[0].score, ranked: entries };
  }

  const probabilities = softmax(logitsOf(model, vector), model.temperature);
  const entries = model.labels
    .map((label, index) => ({
      label,
      score: mapProbability(model, probabilities[index]),
      probability: probabilities[index],
    }))
    .sort((a, b) => b.score - a.score || a.label.localeCompare(b.label));
  return { label: entries[0].label, score: entries[0].score, ranked: entries };
}

// Isotonic map from the calibrated probability to the precision the model actually has at that
// probability: a linear layer gives no 0..1 "similarity" of its own, and the app's thresholds are
// similarity thresholds, so the table is what makes them mean the same thing.
export function fitSimilarityMap(model, scaler, valRows) {
  if (valRows.length === 0) return [[0, 0], [1, 1]];
  const samples = valRows.map((row) => {
    const prediction = predict({ ...model, similarityMap: undefined }, scaler, row.features);
    return { x: prediction.ranked[0].probability, hit: prediction.label === row.label ? 1 : 0 };
  });
  return isotonicMap(samples);
}

export function toSimilarity(map, probability) {
  return applyMap(map, probability);
}
