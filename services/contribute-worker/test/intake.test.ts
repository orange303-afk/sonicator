import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import worker, { MAX_BODY_BYTES, RATE_LIMIT_PER_HOUR, type Env } from "../src/index";

const bindings = env as unknown as Env;

const INSTALL = "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31";
const BATCH = "9b1d6c33-2e58-4f07-8a41-0c7d5e9b2a64";

function record(overrides: Record<string, unknown> = {}) {
  return {
    recordId: "1c4e7a90-5d2b-4f68-b3a1-8e0f2d7c9b45",
    features: Array.from({ length: 14 }, (_, index) => index / 14),
    featureSchema: 1,
    type: "kick",
    typeIsCustom: false,
    tags: [{ name: "acoustic", custom: false }],
    ...overrides,
  };
}

function batch(overrides: Record<string, unknown> = {}) {
  return {
    schema: "contribute.v1",
    installId: INSTALL,
    batchId: BATCH,
    appVersion: "0.4.2",
    os: "mac",
    sentAt: "2026-11-11T10:00:00.000Z",
    records: [record()],
    ...overrides,
  };
}

let nextAddress = 1;

// Every call comes from its own address unless a test is about the rate limit, so one test's
// quota never leaks into another.
function call(body: unknown, init: RequestInit = {}) {
  nextAddress += 1;
  return worker.fetch(
    new Request("https://intake.test/v1/contribute", {
      method: "POST",
      headers: { "content-type": "application/json", "cf-connecting-ip": `198.51.100.${nextAddress % 250}` },
      body: typeof body === "string" ? body : JSON.stringify(body),
      ...init,
    }),
    bindings,
  );
}

async function objectNames(prefix: string) {
  const listing = await bindings.CONTRIBUTE.list({ prefix });
  return listing.objects.map((object) => object.key).sort();
}

describe("intake", () => {
  it("stores a valid batch once and answers 204", async () => {
    const response = await call(batch());
    expect(response.status).toBe(204);
    expect(await objectNames(`raw/${INSTALL}/`)).toEqual([`raw/${INSTALL}/${BATCH}.json`]);
    const stored = await bindings.CONTRIBUTE.get(`raw/${INSTALL}/${BATCH}.json`);
    expect(await stored?.text()).toContain("contribute.v1");
  });

  it("overwrites a retried batch instead of duplicating it", async () => {
    await call(batch());
    await call(batch());
    expect(await objectNames(`raw/${INSTALL}/`)).toHaveLength(1);
  });

  it("rejects unknown fields", async () => {
    expect((await call(batch({ path: "/Users/me/pack/kick.wav" }))).status).toBe(400);
    expect((await call(batch({ records: [record({ fileHash: "abc" })] }))).status).toBe(400);
  });

  it("rejects a wrong feature count, NaN and out-of-range values", async () => {
    expect((await call(batch({ records: [record({ features: [0.1, 0.2] })] }))).status).toBe(400);
    const nan = record();
    nan.features[3] = Number.NaN;
    expect((await call(batch({ records: [nan] }))).status).toBe(400);
    const loud = record();
    loud.features[0] = 9;
    expect((await call(batch({ records: [loud] }))).status).toBe(400);
  });

  it("rejects labels that look like a path or an address", async () => {
    expect((await call(batch({ records: [record({ type: "C:\\\\packs\\\\kick" })] }))).status).toBe(400);
    expect((await call(batch({ records: [record({ tags: [{ name: "me@example.com", custom: true }] })] }))).status)
      .toBe(400);
    expect((await call(batch({ records: [record({ type: "x".repeat(33) })] }))).status).toBe(400);
  });

  it("rejects an oversized body, non-json and a wrong schema", async () => {
    expect((await call("x".repeat(MAX_BODY_BYTES + 1))).status).toBe(413);
    expect(
      (await call(batch(), { headers: { "content-length": String(MAX_BODY_BYTES + 1) } })).status,
    ).toBe(413);
    expect((await call("{")).status).toBe(400);
    expect((await call(batch({ schema: "contribute.v9" }))).status).toBe(400);
    expect((await call(batch({ records: [] }))).status).toBe(400);
  });

  it("rejects a body that is not an object", async () => {
    expect((await call("null")).status).toBe(400);
    expect((await call("[]")).status).toBe(400);
  });

  it("rate limits one address", async () => {
    const address = "192.0.2.77";
    const install = "6a1c0d24-8f3b-4e57-9a2d-7c5e1b9f3a48";
    const fromAddress = (id: string) =>
      worker.fetch(
        new Request("https://intake.test/v1/contribute", {
          method: "POST",
          headers: { "content-type": "application/json", "cf-connecting-ip": address },
          body: JSON.stringify(batch({ batchId: id, installId: install })),
        }),
        bindings,
      );
    for (let index = 0; index < RATE_LIMIT_PER_HOUR; index += 1)
      expect((await fromAddress(crypto.randomUUID())).status).toBe(204);
    expect((await fromAddress(crypto.randomUUID())).status).toBe(429);
  });
});

describe("deletion", () => {
  it("removes every batch of the install id and leaves a tombstone", async () => {
    await call(batch());
    await call(batch({ batchId: "0d9c4b77-6a51-4e2b-9f83-5c1a7e2d8b09" }));
    expect(await objectNames(`raw/${INSTALL}/`)).toHaveLength(2);

    const response = await worker.fetch(
      new Request(`https://intake.test/v1/contributions/${INSTALL}`, {
        method: "DELETE",
        headers: { "cf-connecting-ip": "203.0.113.7" },
      }),
      bindings,
    );
    expect(response.status).toBe(204);
    expect(await objectNames(`raw/${INSTALL}/`)).toEqual([]);
    expect(await objectNames(`deleted/`)).toEqual([`deleted/${INSTALL}`]);
  });

  it("answers 204 for an id with nothing stored", async () => {
    const response = await worker.fetch(
      new Request("https://intake.test/v1/contributions/aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", {
        method: "DELETE",
        headers: { "cf-connecting-ip": "203.0.113.8" },
      }),
      bindings,
    );
    expect(response.status).toBe(204);
  });

  it("refuses a new batch from a deleted install id", async () => {
    await worker.fetch(
      new Request(`https://intake.test/v1/contributions/${INSTALL}`, {
        method: "DELETE",
        headers: { "cf-connecting-ip": "203.0.113.9" },
      }),
      bindings,
    );
    expect((await call(batch())).status).toBe(410);
  });

  it("rejects a malformed install id", async () => {
    const response = await worker.fetch(
      new Request("https://intake.test/v1/contributions/not-a-uuid", { method: "DELETE" }),
      bindings,
    );
    expect(response.status).toBe(400);
  });
});

describe("routes", () => {
  it("answers health", async () => {
    const response = await worker.fetch(new Request("https://intake.test/v1/health"), bindings);
    expect(response.status).toBe(200);
    expect(await response.text()).toBe("ok");
  });

  it("rejects a wrong method and an unknown path", async () => {
    expect((await worker.fetch(new Request("https://intake.test/v1/contribute"), bindings)).status).toBe(405);
    expect((await worker.fetch(new Request("https://intake.test/nope"), bindings)).status).toBe(404);
  });
});
