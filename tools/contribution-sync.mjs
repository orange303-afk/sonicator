#!/usr/bin/env node
// Downloads the contributed training data from the Cloudflare R2 bucket into the project, so the
// training stage can run without touching the network.
//
// Credentials live in the login keychain (service Sonicator.Cloudflare.R2, account
// sonicator-contribute) as JSON with `token` and `accountId`; nothing is written to disk here and
// the token is never printed. The bucket is read-only for this tool: it lists `raw/` and reads the
// batches, and it skips every install that left a tombstone under `deleted/`.
//
// Output (git-ignored, never committed):
//   data/contributions/latest/contribute.jsonl   one line per record, newest version wins
//   data/contributions/latest/report.md         labels, installs, custom share
//   data/contributions/history.csv              one row per run, append-only
//
// Usage: node tools/contribution-sync.mjs [--out DIR] [--min-installs N] [--quiet]

import { execFileSync } from "node:child_process";
import { appendFile, mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(here, "..");
const { latestRecords, readBucket, writeExport } = await import(
  new URL("../services/contribute-worker/scripts/export.mjs", import.meta.url)
);

const KEYCHAIN_SERVICE = "Sonicator.Cloudflare.R2";
const KEYCHAIN_ACCOUNT = "sonicator-contribute";
const BUCKET = "sonicator-contribute";

function argument(argv, name, fallback) {
  const index = argv.indexOf(name);
  if (index < 0) return fallback;
  const value = argv[index + 1];
  if (value === undefined) throw new Error(`${name} needs a value`);
  return value;
}

function credentials() {
  const fromEnvironment = process.env.CLOUDFLARE_API_TOKEN && process.env.CLOUDFLARE_ACCOUNT_ID;
  if (fromEnvironment)
    return { token: process.env.CLOUDFLARE_API_TOKEN, accountId: process.env.CLOUDFLARE_ACCOUNT_ID };

  let raw;
  try {
    raw = execFileSync(
      "security",
      ["find-generic-password", "-s", KEYCHAIN_SERVICE, "-a", KEYCHAIN_ACCOUNT, "-w"],
      { encoding: "utf8" }
    );
  } catch {
    throw new Error(
      `No credentials: nothing in the environment and no keychain item ${KEYCHAIN_SERVICE}/${KEYCHAIN_ACCOUNT}`
    );
  }
  const parsed = JSON.parse(raw);
  if (!parsed.token || !parsed.accountId) throw new Error("Keychain item is missing token or accountId");
  return { token: parsed.token, accountId: parsed.accountId };
}

async function history(outDir, row) {
  const file = join(outDir, "history.csv");
  const header = "generated_at,batches,installs,records,bytes\n";
  let exists = true;
  try {
    await readFile(file, "utf8");
  } catch {
    exists = false;
  }
  if (!exists) await writeFile(file, header, "utf8");
  await appendFile(file, `${row}\n`, "utf8");
}

async function main(argv = process.argv.slice(2)) {
  const outDir = resolve(argument(argv, "--out", join(projectRoot, "data", "contributions")));
  const minInstalls = Number(argument(argv, "--min-installs", "1"));
  const quiet = argv.includes("--quiet");

  const { token, accountId } = credentials();
  const { batches, deleted } = await readBucket({ accountId, token, bucket: BUCKET });
  const records = latestRecords(batches);
  const generatedAt = new Date().toISOString();
  const latestDir = join(outDir, "latest");
  await mkdir(latestDir, { recursive: true });

  const { summary } = await writeExport(records, join(latestDir, "contribute.jsonl"), {
    minInstalls,
    generatedAt,
  });

  const bytes = records.reduce((sum, record) => sum + JSON.stringify(record).length + 1, 0);
  await history(
    outDir,
    [generatedAt, batches.length, summary.installs, records.length, bytes].join(",")
  );

  const installs = new Set(batches.map((batch) => batch.installId)).size;
  const schemas = (summary.featureSchemas ?? []).join("/");
  const line =
    `${records.length} records from ${installs} install(s) in ${batches.length} batches ` +
    `(schema ${schemas || "none"}), ${(bytes / 1048576).toFixed(1)} MB -> ${latestDir}`;
  console.log(quiet ? line : `${line}\nreport: ${join(latestDir, "report.md")}`);
  return 0;
}

const isEntryPoint = process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isEntryPoint)
  main()
    .then((code) => process.exit(code))
    .catch((error) => {
      console.error(error instanceof Error ? error.message : String(error));
      process.exit(1);
    });
