import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { loadManifest, usedDatasets } from "../datasets.mjs";
import { renderCredits } from "../generate-credits.mjs";

const creditsFile = new URL("../../../docs/DATA_CREDITS.md", import.meta.url);
const noticesFile = new URL("../../../THIRD_PARTY_NOTICES.md", import.meta.url);

test("the committed credits file matches the manifest byte for byte", () => {
  assert.equal(readFileSync(creditsFile, "utf8"), renderCredits(loadManifest()));
});

test("every used dataset is named with its licence", () => {
  const text = renderCredits(loadManifest());
  for (const entry of usedDatasets(loadManifest())) {
    assert.ok(text.includes(entry.title), `${entry.id}: title`);
    assert.ok(text.includes(entry.license), `${entry.id}: licence`);
    assert.ok(text.includes(entry.creditLine), `${entry.id}: credit line`);
  }
});

test("excluded datasets are listed as not used, level C with the reason", () => {
  const entries = loadManifest();
  const text = renderCredits(entries);
  const sonniss = entries.find((entry) => entry.id === "sonniss-gdc");
  assert.ok(text.includes("Not used"));
  assert.ok(text.includes(sonniss.notes), "reason from the manifest");
});

test("the notices file points at the credits document", () => {
  assert.match(readFileSync(noticesFile, "utf8"), /docs\/DATA_CREDITS\.md/);
});

test("the credits document is stable regardless of entry order", () => {
  const entries = loadManifest();
  assert.equal(renderCredits([...entries].reverse()), renderCredits(entries));
});

test("the committed authors list is sorted, unique and rendered", async () => {
  const { readFileSync } = await import("node:fs");
  const authors = JSON.parse(readFileSync(new URL("../datasets/cc-by-authors.json", import.meta.url), "utf8"));
  assert.deepEqual(Object.keys(authors).sort(), ["freesound-one-shot-percussive", "fsd50k"]);
  for (const names of Object.values(authors)) {
    assert.ok(names.length > 0, "author list is empty");
    assert.deepEqual(names, [...new Set(names)].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0)));
  }
  assert.match(renderCredits(loadManifest(), authors), /Authors of CC-BY clips/);
});
