import assert from "node:assert/strict";
import test from "node:test";
import {
  buildPrototypes,
  cosine,
  distance,
  indexPrototypes,
  parseDataset,
  predictTags,
  predictType,
  selectPrototypes,
  serializePrototypes,
} from "../lib.mjs";
import { ACOUSTIC, BRIGHT, DARK, dataset, features, record } from "./fixtures.mjs";

test("the same input produces the same bytes", () => {
  const records = dataset(["a", "b", "c"], 5);
  const first = serializePrototypes(buildPrototypes(records));
  const second = serializePrototypes(buildPrototypes(records));
  assert.equal(first, second);
  assert.match(first, /"generatedAt": "2023-11-14T22:13:20.000Z"/);
});

test("a label needs three installs and ten records", () => {
  const types = (records) => buildPrototypes(records).labels.filter((label) => label.kind === "type");
  assert.equal(types(dataset(["a", "b"], 6)).length, 0);
  assert.equal(types(dataset(["a", "b", "c"], 3)).length, 0); // 9 records
  assert.equal(types(dataset(["a", "b", "c"], 4)).length, 1);
});

test("one large install cannot take every prototype", () => {
  const rows = [];
  for (let i = 0; i < 200; i += 1)
    rows.push(record({
      installId: "big",
      recordId: `big-a${String(i).padStart(4, "0")}`,
      features: features(ACOUSTIC),
    }));
  for (let i = 0; i < 200; i += 1)
    rows.push(record({
      installId: "big",
      recordId: `big-b${String(i).padStart(4, "0")}`,
      features: features(BRIGHT),
    }));
  const selected = selectPrototypes(rows, 2);
  assert.equal(selected.length, 2);
  assert.ok(selected.some((feature) => cosine(feature, features(ACOUSTIC)) > 0.99));
  assert.ok(selected.some((feature) => cosine(feature, features(BRIGHT)) > 0.99),
    "the later material of the same install is not crowded out");
});

test("a small install keeps a prototype beside a large one", () => {
  const rows = [];
  for (let i = 0; i < 1000; i += 1)
    rows.push(record({
      installId: "big",
      recordId: `big-${String(i).padStart(4, "0")}`,
      features: features(ACOUSTIC),
    }));
  for (let i = 0; i < 20; i += 1)
    rows.push(record({
      installId: "small",
      recordId: `small-${String(i).padStart(4, "0")}`,
      features: features(BRIGHT),
    }));
  const selected = selectPrototypes(rows, 2);
  assert.equal(selected.length, 2);
  assert.ok(selected.some((feature) => cosine(feature, features(ACOUSTIC)) > 0.99));
  assert.ok(selected.some((feature) => cosine(feature, features(BRIGHT)) > 0.99),
    "twenty records from a second install still count");
});

test("zero vectors and identical rows stay finite and deterministic", () => {
  const rows = [
    record({ recordId: "z1", features: new Array(14).fill(0) }),
    record({ recordId: "z2", features: new Array(14).fill(0) }),
    record({ recordId: "z3", features: features(ACOUSTIC) }),
    record({ recordId: "z4", features: features(ACOUSTIC) }),
  ];
  const first = selectPrototypes(rows, 8);
  const second = selectPrototypes(rows, 8);
  assert.deepEqual(first, second);
  for (const feature of first) for (const value of feature) assert.ok(Number.isFinite(value));
});

test("more prototypes than distinct vectors yields the distinct ones", () => {
  const rows = [1, 2, 3].map((index) => record({ recordId: `r${index}`, features: features(ACOUSTIC) }));
  const selected = selectPrototypes(rows, 8);
  assert.equal(selected.length, 1);
});

test("prototypes stay inside 0..1", () => {
  const rows = dataset(["a", "b", "c"], 30);
  const selected = selectPrototypes(rows, 8);
  assert.ok(selected.length > 0);
  for (const feature of selected)
    for (const value of feature) assert.ok(value >= 0 && value <= 1, `value ${value}`);
});

test("every install carries the same weight when centres are averaged", () => {
  const rows = [];
  for (let i = 0; i < 1000; i += 1)
    rows.push(record({
      installId: "big",
      recordId: `big-${String(i).padStart(4, "0")}`,
      features: features(ACOUSTIC),
    }));
  for (let i = 0; i < 3; i += 1)
    rows.push(record({
      installId: "small",
      recordId: `small-${String(i).padStart(4, "0")}`,
      features: features(BRIGHT),
    }));
  const [centre] = selectPrototypes(rows, 1);
  const acoustic = features(ACOUSTIC);
  const bright = features(BRIGHT);
  const midpoint = acoustic.map((value, index) => (value + bright[index]) / 2);
  assert.ok(cosine(centre, midpoint) > 0.9995,
    `the centre is the mean of the two installs, got cosine ${cosine(centre, midpoint)}`);
});

test("an outlier does not become the centre of a label", () => {
  const rows = [];
  for (let i = 0; i < 30; i += 1)
    rows.push(record({
      installId: `i${i % 3}`,
      recordId: `near-${String(i).padStart(4, "0")}`,
      features: features(ACOUSTIC, 0.005 * (i % 4)),
    }));
  rows.push(record({ installId: "i0", recordId: "zzzz-outlier", features: features(BRIGHT) }));
  const selected = selectPrototypes(rows);
  assert.ok(selected.some((feature) => distance(feature, features(ACOUSTIC)) < 0.01),
    "a majority prototype stays near the cluster");
  assert.ok(selected.filter((feature) => distance(feature, features(BRIGHT)) < 0.01).length <= 1);
});

test("global prototypes win a type where local evidence is missing", () => {
  const records = [
    ...dataset(["a", "b", "c"], 10).map((entry) => ({ ...entry, type: "snare", tags: [] })),
    record({ installId: "d", recordId: "held-out", type: "snare", tags: [], features: features(ACOUSTIC, 0.002) }),
  ];
  const global = indexPrototypes(buildPrototypes(records.filter((entry) => entry.installId !== "d")));
  const local = indexPrototypes(buildPrototypes([], { minInstalls: 1, minSupport: 1 }));
  assert.equal(predictType(features(ACOUSTIC, 0.002), local, global)?.label.toLowerCase(), "snare");
});

test("a local confirmation outranks a near-equal global one", () => {
  const local = indexPrototypes({
    labels: [{ label: "kick", kind: "type", support: 1, installSupport: 1, prototypes: [features(ACOUSTIC)] }],
  });
  const global = indexPrototypes({
    labels: [{ label: "snare", kind: "type", support: 50, installSupport: 5, prototypes: [features(ACOUSTIC, 0.003)] }],
  });
  const winner = predictType(features(ACOUSTIC), local, global);
  assert.equal(winner?.label, "kick");
  assert.equal(winner?.source, "user_adaptive");

  const alone = predictType(features(ACOUSTIC), indexPrototypes({ labels: [] }), global);
  assert.equal(alone?.label, "snare");
  assert.ok(alone.score < 1);
});

test("tags are ranked top-3 and the global penalty is applied", () => {
  const local = indexPrototypes({
    labels: [{ label: "punchy", kind: "tag", support: 5, installSupport: 2, prototypes: [features(ACOUSTIC)] }],
  });
  const global = indexPrototypes({
    labels: [
      { label: "acoustic", kind: "tag", support: 50, installSupport: 5, prototypes: [features(ACOUSTIC, 0.002)] },
      { label: "bright", kind: "tag", support: 50, installSupport: 5, prototypes: [features(BRIGHT)] },
      { label: "dark", kind: "tag", support: 50, installSupport: 5, prototypes: [features(DARK)] },
    ],
  });
  const tags = predictTags(features(ACOUSTIC), local, global);
  assert.equal(tags[0].label, "punchy");
  assert.ok(tags.some((tag) => tag.label === "acoustic"));
  assert.ok(!tags.some((tag) => tag.label === "bright"));
  assert.ok(tags.length <= 3);
});

test("the model date comes from sentAt when the export has no updatedAt", () => {
  const records = dataset(["a", "b", "c"], 4).map((entry) => ({
    ...entry,
    updatedAt: undefined,
    sentAt: "2026-11-11T10:00:00.000Z",
  }));
  assert.equal(buildPrototypes(records).generatedAt, "2026-11-11T10:00:00.000Z");
});

test("a custom type stays out of the shipped file but trains the local model", () => {
  const records = dataset(["a", "b", "c"], 4, { type: "MyLayer", typeIsCustom: true, tags: [] });
  const shipped = buildPrototypes(records);
  assert.equal(shipped.labels.filter((label) => label.kind === "type").length, 0);
  const local = indexPrototypes(buildPrototypes(records, { minInstalls: 1, minSupport: 1, includeCustomTypes: true }));
  assert.ok(local.get("type").has("mylayer"));
});

test("a tag-only record contributes tags and no type", () => {
  const records = dataset(["a", "b", "c"], 4, {
    type: undefined,
    tags: [{ name: "warm", custom: false }],
  });
  const labels = buildPrototypes(records).labels;
  assert.equal(labels.filter((label) => label.kind === "type").length, 0);
  assert.equal(labels.filter((label) => label.kind === "tag").length, 1);
});

test("the dataset reader refuses a foreign feature schema", () => {
  assert.throws(
    () => parseDataset('{"recordId":"x","installId":"a","featureSchema":2,"features":[],"type":"kick"}'),
    /featureSchema/,
  );
});
