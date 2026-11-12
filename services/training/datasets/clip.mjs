import { createHash } from "node:crypto";

// Licence URLs as they appear in the datasets' own metadata files (Freesound licences.txt,
// FSD50K clips info). Stored as a short name so reports do not depend on URL spelling.
const LICENSE_BY_URL = new Map([
  ["http://creativecommons.org/publicdomain/zero/1.0/", "CC0-1.0"],
  ["https://creativecommons.org/publicdomain/zero/1.0/", "CC0-1.0"],
  ["http://creativecommons.org/licenses/by/3.0/", "CC-BY-3.0"],
  ["https://creativecommons.org/licenses/by/3.0/", "CC-BY-3.0"],
  ["http://creativecommons.org/licenses/by/4.0/", "CC-BY-4.0"],
  ["https://creativecommons.org/licenses/by/4.0/", "CC-BY-4.0"],
  ["http://creativecommons.org/licenses/by-nc/3.0/", "CC-BY-NC-3.0"],
  ["https://creativecommons.org/licenses/by-nc/3.0/", "CC-BY-NC-3.0"],
  ["http://creativecommons.org/licenses/by-nc/4.0/", "CC-BY-NC-4.0"],
  ["https://creativecommons.org/licenses/by-nc/4.0/", "CC-BY-NC-4.0"],
  ["http://creativecommons.org/licenses/by-sa/3.0/", "CC-BY-SA-3.0"],
  ["https://creativecommons.org/licenses/by-sa/3.0/", "CC-BY-SA-3.0"],
  ["http://creativecommons.org/licenses/sampling+/1.0/", "CC-Sampling+-1.0"],
  ["https://creativecommons.org/licenses/sampling+/1.0/", "CC-Sampling+-1.0"],
]);

export const GROUPS = ["percussion", "tonal", "vocal", "fx", "foley", "ambience", "loop"];
export const DOMAINS = ["music", "foley", "ambience", "voice"];
export const SPLITS = ["train", "val", "test"];

export function licenseName(url) {
  if (typeof url !== "string") return null;
  return LICENSE_BY_URL.get(url.trim()) ?? null;
}

// Wave 1 takes only CC0 and CC-BY. NC, SA, ND and Sampling+ stay out; level C never enters at all.
export function isShareableLicense(name) {
  return name === "CC0-1.0" || name === "CC-BY-3.0" || name === "CC-BY-4.0";
}

export function tierForLicense(name) {
  return isShareableLicense(name) ? "A" : "B";
}

function requireText(value, where) {
  if (typeof value !== "string" || value.trim().length === 0) throw new Error(`${where} is empty`);
}

export function validateClip(clip) {
  if (clip === null || typeof clip !== "object") throw new Error("clip: not an object");
  requireText(clip.source, "clip: source");
  requireText(clip.clipId, "clip: clipId");
  requireText(clip.file, "clip: file");
  if (clip.label !== null) requireText(clip.label, "clip: label");
  if (!GROUPS.includes(clip.group)) throw new Error(`clip: group ${clip.group} is not one of ${GROUPS.join(", ")}`);
  if (!DOMAINS.includes(clip.domain)) throw new Error(`clip: domain ${clip.domain} is not one of ${DOMAINS.join(", ")}`);
  if (!SPLITS.includes(clip.split)) throw new Error(`clip: split ${clip.split} is not one of ${SPLITS.join(", ")}`);
  if (typeof clip.license !== "string" || clip.license.length === 0) throw new Error("clip: license is empty");
  if (!["A", "B"].includes(clip.tier)) throw new Error(`clip: tier ${clip.tier} is not A or B`);
  return clip;
}

export function makeClip(fields) {
  const { source, clipId, file, label = null, labelRaw, group, domain, licenseUrl, split, instrument, extras } = fields;
  const license = licenseName(licenseUrl);
  if (license === null) throw new Error(`clip: unknown licence ${licenseUrl}`);
  const clip = {
    source,
    clipId: String(clipId),
    file,
    label: label ?? null,
    labelRaw,
    group,
    domain,
    license,
    tier: tierForLicense(license),
    split,
  };
  if (instrument !== undefined) clip.instrument = instrument;
  if (extras !== undefined) clip.extras = extras;
  return validateClip(clip);
}

const byClipId = (a, b) => (a.clipId < b.clipId ? -1 : a.clipId > b.clipId ? 1 : 0);

function capInstrument(clips, perInstrument) {
  if (!Number.isFinite(perInstrument)) return clips;
  const byInstrument = new Map();
  const rest = [];
  for (const clip of clips) {
    if (typeof clip.instrument !== "string") {
      rest.push(clip);
      continue;
    }
    if (!byInstrument.has(clip.instrument)) byInstrument.set(clip.instrument, []);
    byInstrument.get(clip.instrument).push(clip);
  }
  const instruments = [...byInstrument.keys()].sort();
  const deepest = Math.max(0, ...instruments.map((name) => byInstrument.get(name).length));
  const picked = [];
  for (let round = 0; round < Math.min(deepest, perInstrument); round += 1)
    for (const name of instruments) {
      const note = byInstrument.get(name)[round];
      if (note !== undefined) picked.push(note);
    }
  return [...picked, ...rest].sort(byClipId);
}

export function capClips(clips, { perLabel = 2000, perInstrument = Infinity } = {}) {
  const capped = capInstrument([...clips].sort(byClipId), perInstrument);
  const perLabelCount = new Map();
  const kept = [];
  for (const clip of capped) {
    if (clip.label === null) {
      kept.push(clip);
      continue;
    }
    const seen = perLabelCount.get(clip.label) ?? 0;
    if (seen >= perLabel) continue;
    perLabelCount.set(clip.label, seen + 1);
    kept.push(clip);
  }
  return kept;
}

export function splitByUploader(uploader) {
  const digest = createHash("sha256").update(String(uploader)).digest("hex");
  const bucket = Number.parseInt(digest.slice(0, 8), 16) % 100;
  if (bucket < 80) return "train";
  if (bucket < 90) return "val";
  return "test";
}

export function assertUnique(clips, key = (clip) => clip.clipId) {
  const seen = new Set();
  for (const clip of clips) {
    const value = key(clip);
    if (seen.has(value)) throw new Error(`duplicate clip ${value}`);
    seen.add(value);
  }
  return clips;
}
