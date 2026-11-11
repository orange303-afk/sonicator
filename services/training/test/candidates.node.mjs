import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { candidates, renderCandidates } from "../candidates.mjs";
import { STANDARD_TAGS, STANDARD_TYPES } from "../standard.mjs";
import { record } from "./fixtures.mjs";

function customTags(name, installs) {
  return installs.map((installId, index) => record({
    installId,
    recordId: `${installId}-${index}`,
    tags: [{ name, custom: true }],
  }));
}

test("a custom tag needs five installs and one percent of installs", () => {
  const installs = 100;
  const records = [
    ...customTags("Punchy", ["a", "b", "c", "d", "e"]),
    ...customTags("Rare", ["a", "b", "c", "d"]),
  ];
  const list = candidates(records, installs);
  assert.deepEqual(list.map((item) => item.label), ["Punchy"]);
  assert.equal(list[0].installSupport, 5);
  assert.equal(list[0].suggestion, "new tag");
});

test("paths, addresses, numbers and short fragments are dropped", () => {
  const junk = ["/Users/me/pack", "me@example.com", "https://x.test", "12345", "a"];
  const records = junk.flatMap((name, index) => customTags(name, ["a", "b", "c", "d", "e"]).map((entry) => ({
    ...entry,
    recordId: `${index}-${entry.recordId}`,
  })));
  assert.deepEqual(candidates(records, 10), []);
});

test("a case-insensitive match suggests merging into the standard label", () => {
  const records = customTags("KICK", ["a", "b", "c", "d", "e"]);
  for (const entry of records) {
    entry.type = "MyLayer";
    entry.typeIsCustom = true;
  }
  const list = candidates(records, 10);
  const tag = list.find((item) => item.kind === "tag");
  const type = list.find((item) => item.kind === "type");
  assert.equal(tag.suggestion, "merge into Kick");
  assert.equal(type.suggestion, "new type");
});

test("the report names the threshold and the absence of candidates", () => {
  const text = renderCandidates([], { installs: 3, records: 7 });
  assert.match(text, /at least 5 installs/);
  assert.match(text, /No candidate reached the threshold/);
});

test("the standard lists stay in step with the application", async () => {
  const repository = fileURLToPath(new URL("../../../core/database/SampleRepository.cpp", import.meta.url));
  const heuristics = fileURLToPath(new URL("../../../core/smart/NameHeuristics.cpp", import.meta.url));
  const repositorySource = await readFile(repository, "utf8");
  const heuristicSource = await readFile(heuristics, "utf8");
  for (const tag of STANDARD_TAGS)
    assert.ok(repositorySource.includes(`{ "${tag}",`), `SampleRepository.cpp lost the tag ${tag}`);
  for (const type of STANDARD_TYPES)
    assert.ok(heuristicSource.includes(`{ "${type}",`), `NameHeuristics.cpp lost the type ${type}`);
});
