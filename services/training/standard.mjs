// Names that ship with the app. Keep in step with core/database/SampleRepository.cpp (tags) and
// core/smart/NameHeuristics.cpp (types); the test reads both files and fails on drift.
export const STANDARD_TAGS = [
  "Kick", "Snare", "Clap", "Hat", "Percussion", "Bass", "Sub", "Lead", "Pad", "Keys", "Guitar",
  "Vocal", "FX", "Loop", "One-shot", "Crash", "Ride", "Tom", "Rim", "Fill", "Riser",
  "Downfilter", "Upfilter", "Snap", "Shaker", "Tamb",
];

export const STANDARD_TYPES = [
  "kick", "snare", "rim", "clap", "snap", "click", "blip", "zap", "glitch", "hat", "shaker",
  "tamb", "cowbell", "bongo", "conga", "timbale", "agogo", "clave", "cabasa", "triangle",
  "djembe", "tabla", "tom", "crash", "ride", "cymbal", "percussion", "bass", "sub", "808",
  "reese", "acid", "lead", "pad", "keys", "synth", "guitar", "vocal", "fx", "foley", "riser",
  "drop", "brass", "strings",
  // Offered by the Type menu beyond the name heuristics, so they are standard too.
  "arp", "atmosphere", "chord", "downfilter", "drone", "impact", "piano", "pluck", "stab",
  "texture",
];
