import { defineConfig } from "vitest/config";
import { cloudflarePool, cloudflareTest } from "@cloudflare/vitest-plugin";

const options = {
  wrangler: { configPath: "./wrangler.toml" },
  miniflare: { r2Buckets: ["CONTRIBUTE"] },
};

export default defineConfig({
  plugins: [cloudflareTest(options)],
  test: {
    pool: cloudflarePool(options),
  },
});
