import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { capClips, isShareableLicense, licenseName, makeClip } from "../clip.mjs";

const MAP = JSON.parse(readFileSync(new URL("./labels.map.json", import.meta.url), "utf8"));
const GROUP_RULES = MAP.groupRules.map((rule) => ({ ...rule, regex: new RegExp(rule.pattern, "i") }));

function normalize(text) {
  return String(text).toLowerCase().replace(/[^a-z0-9]+/g, " ").trim();
}

export function typeForLabel(label) {
  return MAP.types[label] ?? null;
}

export function groupForLabel(label) {
  const text = normalize(label);
  for (const rule of GROUP_RULES)
    if (rule.regex.test(text)) return { group: rule.group, domain: rule.domain };
  return { ...MAP.fallback };
}

// FSD50K ground truth is a quoted CSV: labels and mids are comma-separated inside quotes.
export function parseCsv(text) {
  const rows = [];
  let row = [];
  let field = "";
  let quoted = false;
  for (let i = 0; i < text.length; i += 1) {
    const char = text[i];
    if (quoted) {
      if (char === '"' && text[i + 1] === '"') {
        field += '"';
        i += 1;
      } else if (char === '"') quoted = false;
      else field += char;
    } else if (char === '"') quoted = true;
    else if (char === ",") {
      row.push(field);
      field = "";
    } else if (char === "\n") {
      row.push(field.replace(/\r$/, ""));
      if (row.some((value) => value.length > 0)) rows.push(row);
      row = [];
      field = "";
    } else field += char;
  }
  if (field.length > 0 || row.length > 0) {
    row.push(field.replace(/\r$/, ""));
    if (row.some((value) => value.length > 0)) rows.push(row);
  }
  const [header, ...rest] = rows;
  return rest.map((values) => Object.fromEntries(header.map((name, index) => [name, values[index] ?? ""])));
}

export function readVocabulary(root) {
  const path = join(root, MAP.datasetDir, "FSD50K.ground_truth", "vocabulary.csv");
  // vocabulary.csv has no header: index,class_name,mid
  return readFileSync(path, "utf8")
    .split("\n")
    .map((line) => line.trim())
    .filter((line) => line.length > 0)
    .map((line) => line.split(",")[1]);
}

function readSplit(root, split) {
  const groundTruth = join(root, MAP.datasetDir, "FSD50K.ground_truth");
  const metadata = join(root, MAP.datasetDir, "FSD50K.metadata");
  const audio = join(root, MAP.datasetDir, `FSD50K.${split}_audio`);
  const rows = parseCsv(readFileSync(join(groundTruth, `${split}.csv`), "utf8"));
  const info = JSON.parse(readFileSync(join(metadata, `${split}_clips_info_FSD50K.json`), "utf8"));
  return { rows, info, audio, split };
}

export function ingest({ root, split }) {
  if (!root) throw new Error("root is required");
  const { rows, info, audio } = readSplit(root, split);
  const skipped = { missingAudio: 0, noLicense: 0, unknownLicense: 0, licenseTier: 0, unmappedLabel: 0 };
  const clips = [];
  for (const row of rows) {
    const clipId = row.fname;
    const file = join(audio, `${clipId}.wav`);
    if (!existsSync(file)) {
      skipped.missingAudio += 1;
      continue;
    }
    const details = info[clipId];
    const license = licenseName(details?.license);
    if (license === null) {
      if (details === undefined) skipped.noLicense += 1;
      else skipped.unknownLicense += 1;
      continue;
    }
    if (!isShareableLicense(license)) {
      skipped.licenseTier += 1;
      continue;
    }
    const labels = row.labels.split(",").map((label) => label.trim()).filter((label) => label.length > 0);
    let label = null;
    for (const candidate of labels) {
      const type = typeForLabel(candidate);
      if (type !== null) {
        label = type;
        break;
      }
    }
    if (label === null) skipped.unmappedLabel += 1;
    const mapped = labels.map((candidate) => groupForLabel(candidate));
    const chosen = mapped.find((entry) => entry.group !== MAP.fallback.group) ?? mapped[0] ??
      { group: MAP.fallback.group, domain: MAP.fallback.domain };
    clips.push(makeClip({
      source: "fsd50k",
      clipId,
      file,
      label,
      labelRaw: labels.join(","),
      group: chosen.group,
      domain: chosen.domain,
      licenseUrl: details.license,
      split: split === "dev" ? row.split : "test",
      extras: { labels, uploader: details.uploader ?? "unknown" },
    }));
  }
  clips.sort((a, b) => (a.clipId < b.clipId ? -1 : a.clipId > b.clipId ? 1 : 0));
  return { clips: capClips(clips, { perLabel: 2000 }), skipped };
}
