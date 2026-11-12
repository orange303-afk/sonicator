import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { basename, extname, join } from "node:path";
import { capClips, isShareableLicense, licenseName, makeClip, splitByUploader } from "../clip.mjs";

const MAP = JSON.parse(readFileSync(new URL("./labels.map.json", import.meta.url), "utf8"));
const COMPILED = MAP.rules.map((rule) => ({ type: rule.type, pattern: new RegExp(rule.pattern, "i") }));

function normalize(text) {
  return String(text).toLowerCase().replace(/[^a-z0-9]+/g, " ").trim();
}

// Names in this dataset are descriptive ("acoustic_closed_hihat1.wav"), tags come from Freesound.
// The name wins: it is the uploader's own word for the sound.
export function labelForClip({ name, tags }) {
  const nameText = normalize(String(name).replace(/\.[a-z0-9]+$/i, ""));
  for (const rule of COMPILED) if (rule.pattern.test(nameText)) return rule.type;
  const tagText = normalize((tags ?? []).join(" "));
  for (const rule of COMPILED) if (rule.pattern.test(tagText)) return rule.type;
  if (/\b(percussion|drums?)\b/.test(tagText)) return "percussion";
  return null;
}

function wavFiles(directory) {
  const files = [];
  if (!existsSync(directory)) return files;
  for (const entry of readdirSync(directory).sort()) {
    const path = join(directory, entry);
    if (statSync(path).isDirectory()) files.push(...wavFiles(path));
    else if (extname(entry).toLowerCase() === ".wav") files.push(path);
  }
  return files;
}

export function ingest({ root }) {
  if (!root) throw new Error("root is required");
  const datasetDir = join(root, MAP.datasetDir);
  const licenses = JSON.parse(readFileSync(join(datasetDir, "licenses.txt"), "utf8"));
  const analysis = JSON.parse(readFileSync(join(datasetDir, "sound_info_analysis.json"), "utf8"));
  const tagsById = new Map(analysis.map((entry) => [String(entry.id), entry.tags ?? []]));

  const skipped = { noLicense: 0, unknownLicense: 0, licenseTier: 0 };
  const clips = [];
  for (const file of wavFiles(join(datasetDir, MAP.audioDir))) {
    const clipId = basename(file, extname(file));
    const entry = licenses[clipId];
    if (entry === undefined) {
      skipped.noLicense += 1;
      continue;
    }
    const license = licenseName(entry.license);
    if (license === null) {
      skipped.unknownLicense += 1;
      continue;
    }
    if (!isShareableLicense(license)) {
      skipped.licenseTier += 1;
      continue;
    }
    const name = entry.name ?? basename(file);
    clips.push(makeClip({
      source: "freesound-one-shot-percussive",
      clipId,
      file,
      label: labelForClip({ name, tags: tagsById.get(clipId) }),
      labelRaw: name,
      group: MAP.group,
      domain: MAP.domain,
      licenseUrl: entry.license,
      split: splitByUploader(entry.username ?? "unknown"),
      extras: { uploader: entry.username ?? "unknown" },
    }));
  }

  clips.sort((a, b) => Number(a.clipId) - Number(b.clipId));
  return { clips: capClips(clips, { perLabel: 2000 }), skipped };
}
