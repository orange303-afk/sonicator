import { readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

// Builds the per-dataset list of authors whose CC-BY clips ended up in the wave. The licence
// condition is attribution to the author, not only to the dataset, so this list is generated from
// the used clips and committed next to the manifest.

function readJsonLines(file) {
  return readFileSync(file, "utf8")
    .split("\n")
    .filter((line) => line.length > 0)
    .map((line) => JSON.parse(line));
}

export function collectAuthors({ root, clips }) {
  const authors = new Map();
  const add = (source, name) => {
    if (typeof name !== "string" || name.trim().length === 0) return;
    if (!authors.has(source)) authors.set(source, new Set());
    authors.get(source).add(name.trim());
  };

  const oneShotLicense = join(root, "freesound-one-shot-percussive", "licenses.txt");
  const oneShot = JSON.parse(readFileSync(oneShotLicense, "utf8"));
  const fsd = {};
  for (const split of ["dev", "eval"])
    Object.assign(fsd, JSON.parse(readFileSync(
      join(root, "fsd50k", "FSD50K.metadata", `${split}_clips_info_FSD50K.json`), "utf8")));

  for (const clip of clips) {
    if (!String(clip.license ?? "").startsWith("CC-BY")) continue;
    if (clip.source === "freesound-one-shot-percussive") add(clip.source, oneShot[clip.clipId]?.username);
    else if (clip.source === "fsd50k") add(clip.source, fsd[clip.clipId]?.uploader);
  }

  return Object.fromEntries([...authors.entries()]
    .map(([source, names]) => [source, [...names].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0))])
    .sort((a, b) => (a[0] < b[0] ? -1 : 1)));
}

function main() {
  const argv = process.argv.slice(2);
  const options = { root: process.env.SONICATOR_DATASETS_ROOT ?? "/Volumes/1TBSSD/Datasets",
    clips: null, out: null };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === "--root") options.root = argv[++i];
    else if (argv[i] === "--clips") options.clips = argv[++i];
    else if (argv[i] === "--out") options.out = argv[++i];
    else throw new Error(`unknown argument: ${argv[i]}`);
  }
  if (options.clips === null || options.out === null) throw new Error("--clips and --out are required");
  const authors = collectAuthors({ root: resolve(options.root), clips: readJsonLines(resolve(options.clips)) });
  writeFileSync(resolve(options.out), `${JSON.stringify(authors, null, 2)}\n`);
  for (const [source, names] of Object.entries(authors))
    process.stdout.write(`${source}: ${names.length} CC-BY authors\n`);
}

const invokedDirectly = process.argv[1] !== undefined
  && pathToFileURL(resolve(process.argv[1])).href === import.meta.url;
if (invokedDirectly) main();
