import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { assertUnique, capClips, makeClip } from "../clip.mjs";

const MAP_PATH = new URL("./labels.map.json", import.meta.url);
const CACHE = new Map();

export function familyMap() {
  const key = "map";
  if (!CACHE.has(key)) CACHE.set(key, JSON.parse(readFileSync(MAP_PATH, "utf8")));
  return CACHE.get(key);
}

export function familyLabel(family) {
  const entry = familyMap().families[family];
  if (entry === undefined) throw new Error(`unknown instrument family: ${family}`);
  return entry;
}

// examples.json is keyed by note_str and carries the family, the instrument and the source
// (acoustic / electronic / synthetic). The audio sits next to it as audio/<note_str>.wav.
export function readExamples(root, split, log = () => {}) {
  const splitDir = join(root, "nsynth", `nsynth-${split}`);
  const examples = JSON.parse(readFileSync(join(splitDir, "examples.json"), "utf8"));
  const map = familyMap();
  const clips = [];
  let missingAudio = 0;
  for (const clipId of Object.keys(examples)) {
    const example = examples[clipId];
    const file = join(splitDir, "audio", `${clipId}.wav`);
    if (!existsSync(file)) {
      missingAudio += 1;
      log(`missing audio ${file}`);
      continue;
    }
    const family = example.instrument_family_str;
    const mapped = familyLabel(family);
    clips.push(makeClip({
      source: "nsynth",
      clipId,
      file,
      label: mapped.label,
      labelRaw: family,
      group: mapped.group,
      domain: map.domain,
      licenseUrl: map.licenseUrl,
      split,
      instrument: example.instrument_str,
      extras: { source: example.instrument_source_str, pitch: example.pitch },
    }));
  }
  return { clips, skipped: { missingAudio } };
}

export function ingest({ root, split }) {
  if (!root) throw new Error("root is required");
  const { clips, skipped } = readExamples(root, split);
  const capped = capClips(clips, { perLabel: 2000, perInstrument: 5 });
  return { clips: assertUnique(capped), skipped };
}
