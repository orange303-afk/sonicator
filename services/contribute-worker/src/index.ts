// Sonicator training-data intake.
//
// Receives a batch of acoustic feature vectors with confirmed labels, validates it and stores it
// as JSON in R2. Nothing that identifies a person, a file or a catalog path is accepted; see
// docs/superpowers/specs/2026-11-11-training-data-contribution-design.md.
//
// There are no secrets here on purpose: the repository and the binary are public, so the bucket is
// reached through a binding and the endpoint itself accepts anonymous writes, guarded by schema
// validation and a per-address rate limit.

export interface Env {
  CONTRIBUTE: R2Bucket;
}

export const BATCH_SCHEMA = "contribute.v1";
export const FEATURE_COUNT = 14;
export const FEATURE_COUNT_V2 = 84;
export const MAX_BODY_BYTES = 256 * 1024;
export const MAX_RECORDS = 500;
export const MAX_TEXT = 32;
export const RATE_LIMIT_PER_HOUR = 30;

const HOUR_MS = 60 * 60 * 1000;
const LIMITER_ENTRIES = 5000;

interface BatchRecord {
  recordId: string;
  features: number[];
  featureSchema: number;
  type?: string;
  typeIsCustom?: boolean;
  tags: { name: string; custom: boolean }[];
  key?: string;
  bpm?: number;
}

interface Batch {
  schema: string;
  installId: string;
  batchId: string;
  appVersion: string;
  os: string;
  sentAt: string;
  records: BatchRecord[];
}

// Per-isolate sliding window. Cloudflare does not promise a single isolate, so this is a cheap
// first line of defence, not an exact quota.
const hits = new Map<string, number[]>();

function rateLimited(address: string): boolean {
  const now = Date.now();
  const recent = (hits.get(address) ?? []).filter((time) => now - time < HOUR_MS);
  if (recent.length >= RATE_LIMIT_PER_HOUR) {
    hits.set(address, recent);
    return true;
  }
  recent.push(now);
  hits.set(address, recent);
  if (hits.size > LIMITER_ENTRIES) {
    for (const [key, times] of hits) {
      if (times.every((time) => now - time >= HOUR_MS)) hits.delete(key);
    }
  }
  return false;
}

function isUuid(value: unknown): value is string {
  return typeof value === "string" &&
    /^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(value);
}

function isText(value: unknown, max = MAX_TEXT): value is string {
  return typeof value === "string" && value.length > 0 && value.length <= max && !value.includes("\u0000");
}

// Labels come from user input, so the only things rejected outright are shapes that would carry a
// file path or an address. Everything else is permitted and reviewed later.
function isLabel(value: unknown): value is string {
  if (!isText(value)) return false;
  const text = value as string;
  return !/[\\/]/.test(text) && !text.includes("@") && !/^[a-z]+:\/\//i.test(text);
}

function isFiniteNumber(value: unknown, min: number, max: number): value is number {
  return typeof value === "number" && Number.isFinite(value) && value >= min && value <= max;
}

function isRecord(value: unknown): value is BatchRecord {
  if (typeof value !== "object" || value === null) return false;
  const record = value as Record<string, unknown>;
  const allowed = new Set(["recordId", "features", "featureSchema", "type", "typeIsCustom", "tags", "key", "bpm"]);
  for (const name of Object.keys(record)) if (!allowed.has(name)) return false;

  if (!isUuid(record.recordId)) return false;
  const expectedFeatures = record.featureSchema === 2 ? FEATURE_COUNT_V2 : FEATURE_COUNT;
  if (!Array.isArray(record.features) || record.features.length !== expectedFeatures) return false;
  if (!record.features.every((item) => isFiniteNumber(item, 0, 1))) return false;
  if (!isFiniteNumber(record.featureSchema, 1, 10_000)) return false;
  if (!Number.isInteger(record.featureSchema)) return false;
  const hasType = record.type !== undefined;
  if (hasType)
  {
    if (!isLabel(record.type)) return false;
    if (typeof record.typeIsCustom !== "boolean") return false;
  }
  else if (record.typeIsCustom !== undefined) return false;
  if (!Array.isArray(record.tags) || record.tags.length > 20) return false;
  if (!hasType && record.tags.length === 0) return false;
  for (const tag of record.tags) {
    if (typeof tag !== "object" || tag === null) return false;
    const entry = tag as { name?: unknown; custom?: unknown };
    if (Object.keys(tag).length !== 2) return false;
    if (!isLabel(entry.name) || typeof entry.custom !== "boolean") return false;
  }
  if (record.key !== undefined && !isLabel(record.key)) return false;
  if (record.bpm !== undefined && !isFiniteNumber(record.bpm, 0, 1000)) return false;
  return true;
}

export function validateBatch(value: unknown): string | null {
  if (typeof value !== "object" || value === null) return "body is not an object";
  const batch = value as Record<string, unknown>;
  const allowed = new Set(["schema", "installId", "batchId", "appVersion", "os", "sentAt", "records"]);
  for (const name of Object.keys(batch)) if (!allowed.has(name)) return `unexpected field ${name}`;

  if (batch.schema !== BATCH_SCHEMA) return "unsupported schema";
  if (!isUuid(batch.installId)) return "installId is not a uuid";
  if (!isUuid(batch.batchId)) return "batchId is not a uuid";
  if (!isText(batch.appVersion)) return "appVersion";
  if (batch.os !== "mac" && batch.os !== "win") return "os";
  if (typeof batch.sentAt !== "string" || Number.isNaN(Date.parse(batch.sentAt))) return "sentAt";
  if (!Array.isArray(batch.records) || batch.records.length === 0) return "records";
  if (batch.records.length > MAX_RECORDS) return "too many records";
  for (const record of batch.records) if (!isRecord(record)) return "invalid record";
  return null;
}

function text(status: number, body: string): Response {
  return new Response(body, { status, headers: { "content-type": "text/plain; charset=utf-8" } });
}

async function contribute(request: Request, env: Env): Promise<Response> {
  const declared = Number(request.headers.get("content-length") ?? "0");
  if (declared > MAX_BODY_BYTES) return text(413, "batch too large");

  const body = await request.text();
  if (body.length > MAX_BODY_BYTES) return text(413, "batch too large");

  let parsed: unknown;
  try {
    parsed = JSON.parse(body);
  } catch {
    return text(400, "body is not json");
  }
  const error = validateBatch(parsed);
  if (error !== null) return text(400, error);

  const batch = parsed as Batch;
  if (await env.CONTRIBUTE.head(`deleted/${batch.installId}`)) return text(410, "install id was deleted");

  // The key is the batch id, so a retry of the same batch overwrites itself instead of duplicating.
  await env.CONTRIBUTE.put(`raw/${batch.installId}/${batch.batchId}.json`, body, {
    httpMetadata: { contentType: "application/json" },
  });
  return new Response(null, { status: 204 });
}

async function removeContributions(installId: string, env: Env): Promise<Response> {
  if (!isUuid(installId)) return text(400, "installId is not a uuid");

  let cursor: string | undefined;
  do {
    const listing = await env.CONTRIBUTE.list({ prefix: `raw/${installId}/`, cursor });
    if (listing.objects.length > 0)
      await env.CONTRIBUTE.delete(listing.objects.map((object) => object.key));
    cursor = listing.truncated ? listing.cursor : undefined;
  } while (cursor !== undefined);

  // A tombstone keeps a later batch under the same id (an old copy of the app, a retry) out of the
  // bucket, and lets the export skip anything already downloaded.
  await env.CONTRIBUTE.put(`deleted/${installId}`, new Date().toISOString(), {
    httpMetadata: { contentType: "text/plain" },
  });
  return new Response(null, { status: 204 });
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    if (url.pathname === "/v1/health") {
      if (request.method !== "GET" && request.method !== "HEAD") return text(405, "method not allowed");
      return new Response("ok", { status: 200 });
    }

    const address = request.headers.get("cf-connecting-ip") ?? "unknown";
    if (rateLimited(address)) return text(429, "too many requests");

    if (url.pathname === "/v1/contribute") {
      if (request.method !== "POST") return text(405, "method not allowed");
      return contribute(request, env);
    }

    const deletion = /^\/v1\/contributions\/([^/]+)$/.exec(url.pathname);
    if (deletion !== null) {
      if (request.method !== "DELETE") return text(405, "method not allowed");
      return removeContributions(deletion[1], env);
    }

    return text(404, "not found");
  },
};
