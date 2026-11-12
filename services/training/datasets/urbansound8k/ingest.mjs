import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { capClips, makeClip } from "../clip.mjs";
import { parseCsv } from "../fsd50k/ingest.mjs";

const MAP = JSON.parse(readFileSync(new URL("./labels.map.json", import.meta.url), "utf8"));
const FALLBACK = { type: null, group: "foley", domain: "foley" };

export function classEntry(name) {
  return MAP.classes[name] ?? FALLBACK;
}

// The official 10 folds are the standard hold-out: 1-8 training, 9 validation, 10 test.
export function splitForFold(fold) {
  if (fold <= 8) return "train";
  if (fold === 9) return "val";
  return "test";
}

export function ingest({ root }) {
  if (!root) throw new Error("root is required");
  const directory = join(root, MAP.datasetDir);
  const rows = parseCsv(readFileSync(join(directory, "metadata", "UrbanSound8K.csv"), "utf8"));
  const skipped = { missingAudio: 0, unknownClass: 0 };
  const clips = [];
  for (const row of rows) {
    const fold = Number(row.fold);
    const file = join(directory, "audio", `fold${fold}`, row.slice_file_name);
    if (!existsSync(file)) {
      skipped.missingAudio += 1;
      continue;
    }
    const entry = classEntry(row.class);
    if (MAP.classes[row.class] === undefined) skipped.unknownClass += 1;
    clips.push(makeClip({
      source: "urbansound8k",
      clipId: row.slice_file_name.replace(/\.[^.]+$/, ""),
      file,
      label: entry.type,
      labelRaw: row.class,
      group: entry.group,
      domain: entry.domain,
      licenseUrl: MAP.licenseUrl,
      split: splitForFold(fold),
      extras: { fold, fsID: row.fsID, salience: Number(row.salience) },
    }));
  }
  clips.sort((a, b) => (a.clipId < b.clipId ? -1 : a.clipId > b.clipId ? 1 : 0));
  return { clips: capClips(clips, { perLabel: 2000 }), skipped };
}
