import assert from "node:assert/strict";
import test from "node:test";
import { capClips, isShareableLicense, licenseName, makeClip, splitByUploader, validateClip } from "../datasets/clip.mjs";

test("only CC0 and CC-BY are shareable", () => {
  assert.equal(licenseName("http://creativecommons.org/publicdomain/zero/1.0/"), "CC0-1.0");
  assert.equal(licenseName("http://creativecommons.org/licenses/by/3.0/"), "CC-BY-3.0");
  assert.equal(licenseName("http://creativecommons.org/licenses/by-nc/3.0/"), "CC-BY-NC-3.0");
  assert.equal(licenseName("http://example.com/none"), null);
  assert.equal(isShareableLicense("CC0-1.0"), true);
  assert.equal(isShareableLicense("CC-BY-3.0"), true);
  assert.equal(isShareableLicense("CC-BY-NC-3.0"), false);
  assert.equal(isShareableLicense("CC-Sampling+-1.0"), false);
  assert.equal(isShareableLicense(null), false);
});

test("clip with a null label is valid, a clip with a foreign group is not", () => {
  const base = makeClip({ source: "nsynth", clipId: "a", file: "/x/a.wav", label: null,
    labelRaw: "mallet", group: "tonal", domain: "music", licenseUrl: "http://creativecommons.org/licenses/by/4.0/",
    split: "train" });
  assert.equal(base.tier, "A");
  assert.equal(validateClip(base).clipId, "a");
  assert.throws(() => validateClip({ ...base, group: "drums" }), /clip: group/);
  assert.throws(() => validateClip({ ...base, source: "" }), /clip: source/);
});

test("caps are deterministic and round-robin across instruments", () => {
  const clips = [];
  for (const instrument of ["bass_a", "bass_b"])
    for (const note of ["03", "01", "02"])
      clips.push({ clipId: `${instrument}-${note}`, instrument, label: "bass" });
  const capped = capClips(clips.reverse(), { perLabel: 4, perInstrument: 2 });
  assert.deepEqual(new Set(capped.map((clip) => clip.clipId)),
    new Set(["bass_a-01", "bass_a-02", "bass_b-01", "bass_b-02"]));
  assert.deepEqual(capClips([...clips].reverse(), { perLabel: 4, perInstrument: 2 }), capped);
});

test("an uploader always lands in the same split", () => {
  const split = splitByUploader("mhc");
  assert.equal(splitByUploader("mhc"), split);
  assert.ok(["train", "val", "test"].includes(split));
  const shares = { train: 0, val: 0, test: 0 };
  for (let i = 0; i < 1000; i += 1) shares[splitByUploader(`user-${i}`)] += 1;
  assert.ok(shares.train > 750 && shares.val > 50 && shares.test > 50);
});
