import assert from "node:assert/strict";
import test from "node:test";
import {
  MAX_RECORDS_PER_INSTALL_PER_LABEL,
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
  for (let i = 0; i < 100; i += 1)
    rows.push(record({
      installId: "big",
      recordId: `big-a-${String(i).padStart(4, "0")}`,
      features: features(ACOUSTIC),
    }));
  for (let i = 0; i < 100; i += 1)
    rows.push(record({
      installId: "big",
      recordId: `big-b-${String(i).padStart(4, "0")}`,
      features: features(BRIGHT),
    }));
  const selected = selectPrototypes(rows);
  assert.ok(selected.length > 0);
  // The first 20 rows by recordId are all in the low cluster, so the high cluster is not used.
  for (const feature of selected) assert.ok(cosine(feature, features(ACOUSTIC)) > 0.99);
  assert.equal(MAX_RECORDS_PER_INSTALL_PER_LABEL, 20);
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

test("the dataset reader refuses a foreign feature schema", () => {
  assert.throws(
    () => parseDataset('{"recordId":"x","installId":"a","featureSchema":2,"features":[],"type":"kick"}'),
    /featureSchema/,
  );
});
