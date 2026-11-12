import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { capClips, makeClip } from "../clip.mjs";
import { parseCsv } from "../fsd50k/ingest.mjs";

const MAP = JSON.parse(readFileSync(new URL("./labels.map.json", import.meta.url), "utf8"));

export function typeForCategory(category) {
  return MAP.types[category] ?? null;
}

export function groupForCategory(category) {
  return MAP.groups[category] ?? { group: "foley", domain: "foley" };
}

// ESC-50 has five official folds for cross-validation, not a hold-out split. Folds 1-3 train,
// 4 validates, 5 tests; the split is recorded per clip so a later wave can change it in one place.
export function splitForFold(fold) {
  if (fold <= 3) return "train";
  if (fold === 4) return "val";
  return "test";
}

export function ingest({ root }) {
  if (!root) throw new Error("root is required");
  const directory = join(root, MAP.datasetDir);
  const rows = parseCsv(readFileSync(join(directory, "meta", "esc50.csv"), "utf8"));
  const skipped = { missingAudio: 0 };
  const clips = [];
  for (const row of rows) {
    const file = join(directory, "audio", row.filename);
    if (!existsSync(file)) {
      skipped.missingAudio += 1;
      continue;
    }
    const type = typeForCategory(row.category);
    const mapped = type === null ? groupForCategory(row.category) : MAP.typeGroups[type];
    clips.push(makeClip({
      source: "esc50",
      clipId: row.filename.replace(/\.[^.]+$/, ""),
      file,
      label: type,
      labelRaw: row.category,
      group: mapped.group,
      domain: mapped.domain,
      licenseUrl: MAP.licenseUrl,
      split: splitForFold(Number(row.fold)),
      extras: { category: row.category, fold: Number(row.fold), esc10: row.esc10 === "True" },
    }));
  }
  clips.sort((a, b) => (a.clipId < b.clipId ? -1 : a.clipId > b.clipId ? 1 : 0));
  return { clips: capClips(clips, { perLabel: 2000 }), skipped };
}
