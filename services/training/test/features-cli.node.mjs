import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

const cli = process.env.SONICATOR_FEATURES_CLI;

function wav(samples, sampleRate = 44100) {
  const data = Buffer.alloc(samples.length * 2);
  samples.forEach((value, index) => data.writeInt16LE(Math.max(-32767, Math.min(32767, Math.round(value * 32767))), index * 2));
  const header = Buffer.alloc(44);
  header.write("RIFF", 0);
  header.writeUInt32LE(36 + data.length, 4);
  header.write("WAVE", 8);
  header.write("fmt ", 12);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(1, 22);
  header.writeUInt32LE(sampleRate, 24);
  header.writeUInt32LE(sampleRate * 2, 28);
  header.writeUInt16LE(2, 32);
  header.writeUInt16LE(16, 34);
  header.write("data", 36);
  header.writeUInt32LE(data.length, 40);
  return Buffer.concat([header, data]);
}

test("the CLI writes one schema-2 line per file", { skip: cli === undefined && "SONICATOR_FEATURES_CLI is not set" }, () => {
  const directory = mkdtempSync(join(tmpdir(), "features-"));
  const rate = 44100;
  const sine = Array.from({ length: rate / 2 }, (_, index) => 0.5 * Math.sin((2 * Math.PI * 440 * index) / rate));
  const noise = Array.from({ length: rate / 2 }, (_, index) => 0.3 * Math.sin(index * 12.9898));
  writeFileSync(join(directory, "sine.wav"), wav(sine, rate));
  writeFileSync(join(directory, "noise.wav"), wav(noise, rate));
  const output = join(directory, "out.jsonl");
  const result = spawnSync(cli, ["--in", directory, "--walk", "--out", output], { encoding: "utf8" });
  assert.equal(result.status, 0, result.stderr);
  const lines = readFileSync(output, "utf8").trim().split("\n").map((line) => JSON.parse(line));
  assert.equal(lines.length, 2);
  for (const line of lines) {
    assert.equal(line.featureSchema, 2);
    assert.equal(line.dimensions, 84);
    assert.equal(line.features.length, 84);
  }
});
