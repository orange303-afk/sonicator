import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import {
  assertValid,
  latestRecords,
  readBucket,
  renderReport,
  summarize,
  writeExport,
} from "../scripts/export.mjs";

const A = "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31";
const B = "4a3b9d2f-8c5e-4b0f-ad30-2e7c9b1f6a42";

function record(overrides = {}) {
  return {
    recordId: "1c4e7a90-5d2b-4f68-b3a1-8e0f2d7c9b45",
    featureSchema: 1,
    features: Array.from({ length: 14 }, () => 0.5),
    type: "kick",
    typeIsCustom: false,
    tags: [{ name: "acoustic", custom: false }],
    ...overrides,
  };
}

function batch(installId, sentAt, records) {
  return { installId, batchId: `${installId}-${sentAt}`, sentAt, appVersion: "0.4.2", records };
}

test("a later batch replaces the label of the same record", () => {
  const older = batch(A, "2026-11-11T10:00:00.000Z", [record()]);
  const newer = batch(A, "2026-11-12T10:00:00.000Z", [record({ type: "snare" })]);
  const records = latestRecords([newer, older]);
  assert.equal(records.length, 1);
  assert.equal(records[0].type, "snare");
  assert.equal(records[0].installId, A);
});

test("a tombstoned install contributes nothing", async () => {
  const responses = new Map([
    [`raw/${A}/b1.json`, batch(A, "2026-11-11T10:00:00.000Z", [record()])],
    [`raw/${B}/b2.json`, batch(B, "2026-11-11T10:00:00.000Z", [record({ recordId: "2c4e7a90-5d2b-4f68-b3a1-8e0f2d7c9b45" })])],
  ]);
  const original = globalThis.fetch;
  globalThis.fetch = async (url) => {
    const text = String(url);
    const json = (value) => new Response(JSON.stringify(value), { status: 200 });
    if (text.includes("prefix=deleted")) return json({ result: [{ key: `deleted/${A}` }], result_info: { is_truncated: false } });
    if (text.includes("prefix=raw")) return json({
      result: [...responses.keys()].map((key) => ({ key })),
      result_info: { is_truncated: false },
    });
    const key = decodeURIComponent(text.split("/objects/")[1]);
    return json(responses.get(key));
  };
  try {
    const { batches, deleted } = await readBucket({ accountId: "acc", token: "t", bucket: "b" });
    assert.deepEqual([...deleted], [A]);
    // The deleted install's batch is never read, so a rebuild drops it even if its objects remain.
    assert.equal(batches.length, 1);
    assert.equal(batches[0].installId, B);
    assert.deepEqual(latestRecords(batches).map((entry) => entry.installId), [B]);
  } finally {
    globalThis.fetch = original;
  }
});

test("the report hides a label that fewer installs confirmed", () => {
  const records = latestRecords([
    batch(A, "2026-11-11T10:00:00.000Z", [record()]),
    batch(B, "2026-11-11T10:00:00.000Z", [record({ recordId: "2c4e7a90-5d2b-4f68-b3a1-8e0f2d7c9b45" })]),
  ]);
  const summary = summarize(records, 3);
  assert.equal(summary.installs, 2);
  assert.equal(summary.records, 2);
  assert.deepEqual(summary.types, []);
  assert.deepEqual(summary.tags, []);

  const looser = summarize(records, 1);
  assert.equal(looser.types.length, 1);
  assert.equal(looser.types[0].installSupport, 2);
});

test("out-of-range features and an unknown schema stop the export", () => {
  assert.throws(() => assertValid([{ ...record({ features: [0.5, 1.5] }), installId: A }]), /14 features/);
  const bad = record();
  bad.features[3] = Number.NaN;
  assert.throws(() => assertValid([{ ...bad, installId: A }]), /outside 0\.\.1/);
  assert.throws(() => assertValid([{ ...record({ featureSchema: 3 }), installId: A }]), /featureSchema/);
  const v2 = { ...record({ features: Array(84).fill(0.5) }), featureSchema: 2, installId: A };
  assertValid([v2]);
});

test("a tag-only record is valid and contributes tags, not a type", () => {
  const tagOnly = record({ type: undefined, typeIsCustom: undefined, tags: [{ name: "warm", custom: false }] });
  assertValid([{ ...tagOnly, installId: A }]);
  const summary = summarize([{ ...tagOnly, installId: A }], 1);
  assert.equal(summary.types.length, 0);
  assert.equal(summary.tags.length, 1);
  assert.throws(
    () => assertValid([{ ...record({ type: undefined, typeIsCustom: undefined, tags: [] }), installId: A }]),
    /no type and no tags/,
  );
});

test("the same records produce the same bytes and a report beside the dataset", async () => {
  const dir = await mkdtemp(join(tmpdir(), "sonicator-export-"));
  try {
    const records = latestRecords([
      batch(A, "2026-11-11T10:00:00.000Z", [record()]),
      batch(B, "2026-11-11T10:00:00.000Z", [
        record({ recordId: "2c4e7a90-5d2b-4f68-b3a1-8e0f2d7c9b45", type: "snare" }),
      ]),
    ]);
    const first = join(dir, "one.jsonl");
    const second = join(dir, "two.jsonl");
    const generatedAt = "2026-11-11T00:00:00Z";
    await writeExport(records, first, { generatedAt });
    await writeExport(records, second, { generatedAt });
    assert.equal(await readFile(first, "utf8"), await readFile(second, "utf8"));

    const report = await readFile(join(dir, "report.md"), "utf8");
    assert.match(report, /Installs: 2/);
    assert.match(report, /Records: 2/);
    const strict = renderReport(summarize(records, 3), { generatedAt });
    assert.match(strict, /No Type reached/);
    assert.match(strict, /No Tag reached/);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
});
