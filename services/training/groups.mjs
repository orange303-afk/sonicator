// One fixed grouping for the report. Changing it changes the per-group numbers, so a change is
// recorded in the plan's report rather than slipped in.
export const TYPE_GROUPS = {
  percussion: [
    "kick", "snare", "rim", "clap", "snap", "click", "blip", "zap", "hat", "shaker", "tamb",
    "cowbell", "bongo", "conga", "timbale", "agogo", "clave", "cabasa", "triangle", "djembe",
    "tabla", "tom", "crash", "ride", "cymbal", "percussion",
  ],
  tonal: [
    "bass", "sub", "808", "reese", "acid", "lead", "pad", "keys", "synth", "guitar", "brass",
    "strings",
  ],
  vocal: ["vocal"],
  fx: ["fx", "foley", "riser", "drop", "glitch"],
};

const LOOKUP = new Map();
for (const [group, names] of Object.entries(TYPE_GROUPS))
  for (const name of names) LOOKUP.set(name.toLowerCase(), group);

export function groupForType(name) {
  return LOOKUP.get(String(name).toLowerCase()) ?? "other";
}
