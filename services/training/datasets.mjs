import { readFileSync } from "node:fs";

// Path inside the repository, so the manifest can also be printed and diffed from the repo root.
export const MANIFEST_PATH = "services/training/datasets/manifest.json";

const TIERS = ["A", "B", "C"];
const REQUIRED_TEXT = ["id", "title", "version", "license", "creditLine"];

// Throws on the first entry that could not be used honestly: a missing licence, an unknown tier, a
// duplicate id, or a level C dataset marked as used (its licence forbids training on it).
export function validateManifest(entries) {
  if (!Array.isArray(entries)) throw new Error("manifest: expected an array of entries");
  const seen = new Set();
  for (const entry of entries) {
    if (entry === null || typeof entry !== "object") throw new Error("manifest: entry is not an object");
    for (const key of REQUIRED_TEXT)
      if (typeof entry[key] !== "string" || entry[key].trim().length === 0)
        throw new Error(`manifest: ${entry.id ?? "?"}: ${key} is empty`);
    if (typeof entry.url !== "string" || !entry.url.startsWith("https://"))
      throw new Error(`manifest: ${entry.id}: url must be https`);
    if (!TIERS.includes(entry.tier)) throw new Error(`manifest: ${entry.id}: tier ${entry.tier} is not A, B or C`);
    if (typeof entry.used !== "boolean") throw new Error(`manifest: ${entry.id}: used must be boolean`);
    if (typeof entry.licensePerClip !== "boolean")
      throw new Error(`manifest: ${entry.id}: licensePerClip must be boolean`);
    if (entry.used && entry.tier === "C") throw new Error(`manifest: ${entry.id}: level C dataset cannot be used`);
    if (seen.has(entry.id)) throw new Error(`manifest: duplicate id ${entry.id}`);
    seen.add(entry.id);
  }
  return entries;
}

export function usedDatasets(entries) {
  return entries.filter((entry) => entry.used);
}

function resolvePath(path) {
  return path.startsWith("/") ? path : new URL(`../../${path}`, import.meta.url);
}

export function loadManifest(path = MANIFEST_PATH) {
  return validateManifest(JSON.parse(readFileSync(resolvePath(path), "utf8")));
}
