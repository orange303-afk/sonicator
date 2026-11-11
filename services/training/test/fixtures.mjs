// Synthetic feature shapes. Real vectors vary per dimension; these three point in clearly
// different directions so a test can tell a near match from an unrelated one.

export const ACOUSTIC = [0.7, 0.65, 0.6, 0.55, 0.5, 0.45, 0.4, 0.35, 0.3, 0.25, 0.2, 0.15, 0.1, 0.05];
export const BRIGHT = [0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7];
export const DARK = [0.9, 0.1, 0.85, 0.15, 0.8, 0.2, 0.75, 0.25, 0.7, 0.3, 0.65, 0.35, 0.6, 0.4];

const clamp = (value) => Math.min(1, Math.max(0, value));

export function features(base, jitter = 0) {
  return base.map((value, index) => clamp(value + jitter * ((index % 5) - 2) / 2));
}

let counter = 0;
export function record(overrides = {}) {
  counter += 1;
  return {
    recordId: `00000000-0000-4000-8000-${String(counter).padStart(12, "0")}`,
    installId: "install-a",
    featureSchema: 1,
    features: features(ACOUSTIC),
    type: "kick",
    typeIsCustom: false,
    tags: [{ name: "acoustic", custom: false }],
    updatedAt: 1700000000000,
    ...overrides,
  };
}

export function dataset(installs, perInstall, overrides = {}) {
  const out = [];
  for (const installId of installs)
    for (let i = 0; i < perInstall; i += 1)
      out.push(record({
        installId,
        recordId: `${installId}-${String(i).padStart(6, "0")}`,
        features: features(ACOUSTIC, 0.01 * i),
        ...overrides,
      }));
  return out;
}
