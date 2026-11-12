import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, symlinkSync, writeFileSync } from "node:fs";
import { cpus } from "node:os";
import { homedir } from "node:os";
import { basename, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { ingest as ingestOneShot } from "./datasets/freesound-one-shot-percussive/ingest.mjs";
import { ingest as ingestFsd50k } from "./datasets/fsd50k/ingest.mjs";
import { ingest as ingestNsynth } from "./datasets/nsynth/ingest.mjs";

const DEFAULT_ROOT = "/Volumes/1TBSSD/Datasets";
const DEFAULT_CLI = join(homedir(), "Library/Caches/sonicator-build/release/tools/sonicator-features/",
  "sonicator-features_artefacts/Release/sonicator-features");
const FEATURE_SCHEMA_V2 = 2;
const FEATURE_DIMENSIONS_V2 = 84;

export function validateFeatureLine(line) {
  if (line === null || typeof line !== "object") throw new Error("line is not an object");
  if (line.featureSchema !== FEATURE_SCHEMA_V2)
    throw new Error(`featureSchema ${line.featureSchema} is not ${FEATURE_SCHEMA_V2}`);
  if (line.dimensions !== FEATURE_DIMENSIONS_V2)
    throw new Error(`dimensions ${line.dimensions} is not ${FEATURE_DIMENSIONS_V2}`);
  if (!Array.isArray(line.features) || line.features.length !== FEATURE_DIMENSIONS_V2)
    throw new Error(`features has ${line.features?.length} values`);
  for (const value of line.features)
    if (typeof value !== "number" || !Number.isFinite(value) || value < 0 || value > 1)
      throw new Error(`feature ${value} is outside 0..1`);
  if (line.inBandEnergyShare !== undefined
    && (typeof line.inBandEnergyShare !== "number" || !Number.isFinite(line.inBandEnergyShare)
      || line.inBandEnergyShare < 0 || line.inBandEnergyShare > 1))
    throw new Error(`inBandEnergyShare ${line.inBandEnergyShare} is outside 0..1`);
  return line;
}

export function joinRows({ lines, clipsByPath }) {
  const rows = [];
  const unmatched = [];
  for (const line of lines) {
    validateFeatureLine(line);
    const clip = clipsByPath.get(line.path);
    if (clip === undefined) {
      unmatched.push(line.path);
      continue;
    }
    const { path: _path, ...features } = line;
    rows.push({ ...clip, ...features });
  }
  return { rows, unmatched };
}

export function planShards(clips) {
  const shards = new Map();
  for (const clip of clips) {
    const name = `${clip.source}-${clip.split}`;
    if (!shards.has(name)) shards.set(name, { name, source: clip.source, split: clip.split, clips: [] });
    shards.get(name).clips.push(clip);
  }
  return [...shards.values()]
    .map((shard) => ({ ...shard, clips: [...shard.clips].sort((a, b) => (a.clipId < b.clipId ? -1 : 1)) }))
    .sort((a, b) => (a.name < b.name ? -1 : 1));
}

function countLines(file) {
  const text = readFileSync(file, "utf8");
  return text.length === 0 ? 0 : text.split("\n").filter((line) => line.length > 0).length;
}

// A shard is reusable only when its marker exists AND the file still carries one line per link:
// a killed CLI leaves a truncated file that the marker alone would hide.
export function isShardDone({ out, shard, expected }) {
  const marker = join(out, `${shard}.done`);
  const file = join(out, `${shard}.features.jsonl`);
  if (!existsSync(marker) || !existsSync(file)) return false;
  return countLines(file) === expected;
}

function parseArgs(argv) {
  const options = { root: process.env.SONICATOR_DATASETS_ROOT ?? DEFAULT_ROOT, out: null, jobs: Math.min(4, cpus().length),
    source: "all", cli: process.env.SONICATOR_FEATURES_CLI ?? DEFAULT_CLI, full: false };
  for (let i = 0; i < argv.length; i += 1) {
    const argument = argv[i];
    const value = () => {
      if (i + 1 >= argv.length) throw new Error(`${argument} needs a value`);
      return argv[++i];
    };
    if (argument === "--root") options.root = value();
    else if (argument === "--out") options.out = value();
    else if (argument === "--jobs") options.jobs = Number(value());
    else if (argument === "--source") options.source = value();
    else if (argument === "--cli") options.cli = value();
    else if (argument === "--full") options.full = true;
    else throw new Error(`unknown argument: ${argument}`);
  }
  if (options.out === null) throw new Error("--out is required");
  return options;
}

function ingestSources({ root, source }) {
  const collected = { clips: [], skipped: {}, seen: {} };
  const add = (name, result) => {
    const clips = Array.isArray(result) ? result : result.clips;
    collected.clips.push(...clips);
    collected.skipped[name] = result.skipped ?? {};
    collected.seen[name] = clips.length;
    process.stderr.write(`ingest ${name}: ${clips.length} clips\n`);
  };
  if (source === "all" || source === "nsynth")
    for (const split of ["train", "valid", "test"]) add(`nsynth-${split}`, ingestNsynth({ root, split }));
  if (source === "all" || source === "one-shot") add("one-shot", ingestOneShot({ root }));
  if (source === "all" || source === "fsd50k")
    for (const split of ["dev", "eval"]) add(`fsd50k-${split}`, ingestFsd50k({ root, split }));
  collected.clips.sort((a, b) => {
    if (a.source !== b.source) return a.source < b.source ? -1 : 1;
    if (a.split !== b.split) return a.split < b.split ? -1 : 1;
    return a.clipId < b.clipId ? -1 : a.clipId > b.clipId ? 1 : 0;
  });
  return collected;
}

function datasetDirectory(root, source, split) {
  if (source === "nsynth") return join(root, "nsynth", `nsynth-${split}`);
  if (source === "fsd50k") return join(root, "fsd50k", `FSD50K.${split}_audio`);
  return join(root, "freesound-one-shot-percussive", "one_shot_percussive_sounds");
}

function runCli({ cli, input, output }) {
  return new Promise((resolvePromise, reject) => {
    const child = spawn(cli, ["--in", input, "--walk", "--out", output], { stdio: ["ignore", "ignore", "pipe"] });
    let stderr = "";
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString();
      if (stderr.length > 64 * 1024) stderr = stderr.slice(-64 * 1024);
    });
    child.on("error", reject);
    child.on("close", (code) => {
      writeFileSync(`${output}.cli.log`, stderr);
      if (code !== 0) reject(new Error(`${cli} failed with exit ${code}: ${stderr.slice(-400)}`));
      else resolvePromise();
    });
  });
}

async function runPool(items, jobs, worker) {
  const queue = [...items];
  let failure = null;
  const runners = Array.from({ length: Math.max(1, Math.min(jobs, queue.length)) }, async () => {
    while (queue.length > 0 && failure === null) {
      const item = queue.shift();
      try {
        await worker(item);
      } catch (error) {
        failure = error;
      }
    }
  });
  await Promise.all(runners);
  if (failure !== null) throw failure;
}

function parseJsonLines(file) {
  return readFileSync(file, "utf8")
    .split("\n")
    .filter((line) => line.length > 0)
    .map((line, index) => {
      try {
        return JSON.parse(line);
      } catch (error) {
        throw new Error(`${file}:${index + 1}: ${error.message}`);
      }
    });
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const root = resolve(options.root);
  const out = resolve(options.out);
  mkdirSync(out, { recursive: true });

  const collected = ingestSources({ root, source: options.source });
  const clips = collected.clips;
  writeFileSync(join(out, "clips.jsonl"), clips.map((clip) => JSON.stringify(clip)).join("\n") + "\n");

  const shards = planShards(clips);
  const expected = new Map(shards.map((shard) => [shard.name, shard.clips.length]));
  const paths = new Map();

  for (const shard of shards) {
    const directory = options.full
      ? datasetDirectory(root, shard.source, shard.split)
      : join(out, "links", shard.name);
    if (!existsSync(directory)) throw new Error(`no directory ${directory}`);
    if (!options.full) {
      rmSync(directory, { recursive: true, force: true });
      mkdirSync(directory, { recursive: true });
      shard.clips.forEach((clip, index) => {
        const link = join(directory, `${String(index).padStart(6, "0")}-${basename(clip.file)}`);
        symlinkSync(clip.file, link);
        paths.set(link, clip);
      });
    } else {
      for (const clip of shard.clips) paths.set(clip.file, clip);
    }
    shard.directory = directory;
  }

  await runPool(shards, options.jobs, async (shard) => {
    const output = join(out, `${shard.name}.features.jsonl`);
    if (isShardDone({ out, shard: shard.name, expected: expected.get(shard.name) })) {
      process.stderr.write(`shard ${shard.name}: already done\n`);
      return;
    }
    const started = Date.now();
    process.stderr.write(`shard ${shard.name}: ${shard.clips.length} files\n`);
    await runCli({ cli: options.cli, input: shard.directory, output });
    const lines = countLines(output);
    if (lines !== expected.get(shard.name))
      process.stderr.write(`shard ${shard.name}: CLI wrote ${lines} of ${expected.get(shard.name)} lines\n`);
    writeFileSync(join(out, `${shard.name}.done`), `${lines}\n`);
    process.stderr.write(`shard ${shard.name}: done in ${Math.round((Date.now() - started) / 1000)}s\n`);
  });

  const rows = [];
  const unmatched = [];
  for (const shard of shards) {
    const file = join(out, `${shard.name}.features.jsonl`);
    if (!existsSync(file)) continue;
    const joined = joinRows({ lines: parseJsonLines(file), clipsByPath: paths });
    rows.push(...joined.rows);
    unmatched.push(...joined.unmatched);
  }
  rows.sort((a, b) => {
    if (a.source !== b.source) return a.source < b.source ? -1 : 1;
    if (a.split !== b.split) return a.split < b.split ? -1 : 1;
    return a.clipId < b.clipId ? -1 : a.clipId > b.clipId ? 1 : 0;
  });
  writeFileSync(join(out, "features.jsonl"), rows.map((row) => JSON.stringify(row)).join("\n") + "\n");

  const summary = {
    generatedFrom: root,
    clips: clips.length,
    rows: rows.length,
    unmatched: unmatched.length,
    perSource: Object.fromEntries(Object.entries(collected.seen)),
    skipped: collected.skipped,
    dimensions: FEATURE_DIMENSIONS_V2,
    featureSchema: FEATURE_SCHEMA_V2,
  };
  writeFileSync(join(out, "build.json"), `${JSON.stringify(summary, null, 2)}\n`);
  process.stderr.write(`features: ${rows.length} rows, ${unmatched.length} unmatched, ${clips.length} clips\n`);
}

const invokedDirectly = process.argv[1] !== undefined
  && pathToFileURL(resolve(process.argv[1])).href === import.meta.url;
if (invokedDirectly) main().catch((error) => {
  process.stderr.write(`${error.message}\n`);
  process.exit(1);
});
