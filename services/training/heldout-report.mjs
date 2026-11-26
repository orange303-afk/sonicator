// Markdown for the held-out evaluation. Every number comes from evaluateContributed(); this file only
// arranges it, so two runs on the same export produce the same text byte for byte.

import { LOCAL_PRIORITY_MARGIN } from "./lib.mjs";

const MODE_TITLE = {
  cold: "cold start",
  app: "with own confirmations",
  local: "own confirmations only",
};

const MODE_FLOW = ["cold", "app", "local"];

function pct(value) {
  return `${(value * 100).toFixed(1)}%`;
}

function interval(bounds) {
  return `${pct(bounds.low)}–${pct(bounds.high)}`;
}

function shortInstall(id) {
  return id.length <= 12 ? id : id.slice(0, 8);
}

function table(header, rows) {
  const head = `| ${header.join(" | ")} |`;
  const rule = `|${header.map(() => "---").join("|")}|`;
  return [head, rule, ...rows.map((row) => `| ${row.join(" | ")} |`)].join("\n");
}

function heading(level, text) {
  return `${"#".repeat(level)} ${text}`;
}

function modeRow(mode, metrics) {
  return [
    MODE_TITLE[mode],
    metrics.records,
    pct(metrics.top1),
    pct(metrics.top3),
    pct(metrics.coverage),
    `${pct(metrics.precision)} (${interval(metrics.precisionInterval)})`,
  ];
}

function summaryBlock(summary, level) {
  if (summary.named.cold.records === 0) return "";
  const lines = [heading(level, "Summary"), ""];
  lines.push("`In-vocabulary` counts only rows whose label is one of the model's own; `all standard` adds the");
  lines.push("standard types it cannot name, so there a vocabulary gap is a miss, as it is for a person.");
  lines.push("");
  for (const [title, metrics] of [["Rows the model can name", summary.named], ["All standard types", summary.standard]]) {
    lines.push(`**${title}**`);
    lines.push("");
    lines.push(table(
      ["mode", "records", "top-1", "top-3", "coverage", "precision at acceptance (95% CI)"],
      MODE_FLOW.map((mode) => modeRow(mode, metrics[mode])),
    ));
    lines.push("");
  }
  return lines.join("\n");
}

function installsBlock(summary, data, level) {
  const exportRows = data.stats.exportByInstall ?? {};
  if (summary.installs.length === 0) return "";
  const lines = [heading(level, "Per install"), ""];
  lines.push("`Export rows` counts everything the install sent, any schema; `records` counts the rows this");
  lines.push("block can score. A row with no interval is a row with no answer to score.");
  lines.push("");
  for (const mode of MODE_FLOW) {
    lines.push(`**${MODE_TITLE[mode][0].toUpperCase()}${MODE_TITLE[mode].slice(1)}**`);
    lines.push("");
    lines.push(table(
      [
        "install", "versions", "export rows", "records", "in-vocabulary", "top-1 (in-vocabulary)",
        "coverage (all standard)", "precision (all standard)", "local evidence",
      ],
      summary.installs.map((install) => [
        `\`${shortInstall(install.installId)}\``,
        install.versions.join(", ") || "—",
        exportRows[install.installId] ?? install.records.all,
        install.records.all,
        install.records.model,
        install.named[mode].records === 0 ? "—" : pct(install.named[mode].top1),
        install.standard[mode].records === 0 ? "—" : pct(install.standard[mode].coverage),
        install.standard[mode].accepted === 0
          ? "—"
          : `${pct(install.standard[mode].precision)} (${interval(install.standard[mode].precisionInterval)})`,
        install.records.all === 0 ? "—" : pct(install.localEvidence / install.records.all),
      ]),
    ));
    lines.push("");
  }
  return lines.join("\n");
}

function localBlock(summary, level) {
  const evidence = summary.localEvidence;
  if (evidence.records === 0) return "";
  const cold = summary.named.cold;
  const app = summary.named.app;
  const local = summary.named.local;
  const sum = (field) => summary.installs.reduce((total, install) => total + install[field], 0);
  const appWinsLocal = sum("appLocalWins");
  const appWinsTotal = appWinsLocal + sum("appGlobalWins");
  const localPublished = sum("appLocalPublished");
  const localCorrect = sum("appLocalCorrect");
  const lines = [heading(level, "The two scales at the acceptance threshold"), ""];
  lines.push(
    `Rows with a local prototype that is not a copy of the same audio: **${evidence.recordsWithLocal}** of ${evidence.records}. ` +
    `Copies skipped: **${evidence.twins}** prototypes whose vector equals the row's own.`,
  );
  lines.push("");
  lines.push(`Median best local cosine: **${evidence.medianBest.toFixed(3)}**.`);
  lines.push("");
  lines.push(table(
    ["evidence", "records", "top-1", "coverage", "precision"],
    [
      ["model alone, cold start", cold.records, pct(cold.top1), pct(cold.coverage), pct(cold.precision)],
      ["the two merged, as the app does", app.records, pct(app.top1), pct(app.coverage), pct(app.precision)],
      ["local cosine alone", local.records, pct(local.top1), pct(local.coverage), pct(local.precision)],
    ],
  ));
  lines.push("");
  if (appWinsTotal > 0) {
    lines.push(`The local label wins **${appWinsLocal}** of ${appWinsTotal} rows.`);
    if (localPublished > 0)
      lines.push(`Of those wins, **${localPublished}** were published, and **${localCorrect}** were right ` +
        `(${pct(localCorrect / localPublished)}).`);
    lines.push("");
  }
  if (evidence.medianBest >= summary.threshold) {
    lines.push(
      `The median best cosine is already above the ${summary.threshold.toFixed(2)} threshold, so the threshold ` +
      "cannot gate a local answer there: it decides nothing. The global score is mapped to the precision it " +
      "carries, the local cosine is a similarity, so the merge compares a calibrated number with a raw one " +
      "and the raw one wins almost every time.",
    );
  } else {
    lines.push(
      "The local cosine is not calibrated: it is a similarity, while the global score is mapped to the " +
      `precision it carries. The merge compares the two as if they meant the same thing (0.95 penalty, ` +
      `${LOCAL_PRIORITY_MARGIN.toFixed(2)} margin), so a local answer above ${summary.threshold.toFixed(2)} is ` +
      "published even when its real precision is lower.",
    );
  }
  lines.push("");
  return lines.join("\n");
}

function curveBlock(summary, level) {
  if (summary.named.cold.records === 0) return "";
  const named = summary.named.cold.curves;
  const standard = summary.standard.cold.curves;
  const lines = [heading(level, "What the acceptance threshold decides, cold start"), ""];
  lines.push("The threshold is a trade: a lower one publishes more answers and is right less often. Nothing");
  lines.push("here changes it; the table is what a decision would be made on.");
  lines.push("");
  lines.push(table(
    ["threshold", "coverage, labels the model can name", "precision there", "coverage, all standard", "precision there"],
    named.map((entry, index) => [
      entry.threshold.toFixed(2),
      pct(entry.coverage),
      pct(entry.precision),
      pct(standard[index].coverage),
      pct(standard[index].precision),
    ]),
  ));
  lines.push("");
  return lines.join("\n");
}

function labelsBlock(summary, level) {
  if (summary.perLabel.length === 0) return "";
  const lines = [heading(level, "Per type, cold start"), ""];
  lines.push(table(
    ["type", "records", "accepted", "precision", "top-1", "top-3", "mean score", "most common accepted mistake"],
    summary.perLabel.map((entry) => {
      const cold = entry.modes.cold;
      const mistake = cold.wrong[0];
      return [
        `\`${entry.label}\``,
        entry.records,
        cold.accepted === 0 ? "—" : `${cold.accepted} (${pct(cold.coverage)})`,
        cold.accepted === 0 ? "—" : pct(cold.precision),
        pct(cold.top1),
        pct(cold.top3),
        cold.meanScore.toFixed(3),
        mistake === undefined ? "—" : `${mistake.label} ×${mistake.count}`,
      ];
    }),
  ));
  lines.push("");
  return lines.join("\n");
}

function missingBlock(summary, level) {
  const lines = [heading(level, "Types the model cannot name"), ""];
  if (summary.missing.length === 0) {
    lines.push("No row in this set uses a standard type outside the model's labels.");
    lines.push("");
    return lines.join("\n");
  }
  lines.push("These labels sit outside the model's vocabulary, so every answer it gives them is one of its");
  lines.push("own types. This is the vocabulary gap, not a ranking mistake.");
  lines.push("");
  lines.push(table(
    ["type", "records", "installs", "accepted", "what the model answers (cold start)"],
    summary.missing.map((entry) => {
      const cold = entry.modes.cold;
      const top = entry.topCold.slice(0, 3).map((item) => `${item.label} ×${item.count}`).join(", ");
      return [
        `\`${entry.label}\``,
        entry.records,
        entry.installs.length,
        cold.records === 0 ? "—" : `${cold.accepted} (${pct(cold.coverage)})`,
        top.length === 0 ? "—" : top,
      ];
    }),
  ));
  lines.push("");
  return lines.join("\n");
}

function customBlock(summary, level) {
  const lines = [heading(level, "Custom types"), ""];
  if (summary.custom.length === 0 && summary.unknown.length === 0) {
    lines.push("No row in this set uses a custom type name.");
    lines.push("");
    return lines.join("\n");
  }
  lines.push("A custom type cannot come from the shipped model at all. Locally, however, the person's own");
  lines.push("confirmed rows become prototypes for that name, which is why the local columns can be non-zero.");
  lines.push("");
  if (summary.custom.length > 0) {
    lines.push(table(
      ["type", "records", "installs", "cold start answer", "local top-1", "local coverage"],
      summary.custom.map((entry) => [
        `\`${entry.label}\``,
        entry.records,
        entry.installs.length,
        entry.topCold.slice(0, 2).map((item) => `${item.label} ×${item.count}`).join(", ") || "—",
        entry.modes.app.records === 0 ? "—" : pct(entry.modes.app.top1),
        entry.modes.app.records === 0 ? "—" : pct(entry.modes.app.coverage),
      ]),
    ));
    lines.push("");
  }
  if (summary.unknown.length > 0) {
    lines.push("Names that are neither standard nor marked custom:");
    lines.push("");
    lines.push(table(
      ["type", "records", "installs", "cold start answer"],
      summary.unknown.map((entry) => [
        `\`${entry.label}\``,
        entry.records,
        entry.installs.length,
        entry.topCold.slice(0, 2).map((item) => `${item.label} ×${item.count}`).join(", ") || "—",
      ]),
    ));
    lines.push("");
  }
  return lines.join("\n");
}

function calibrationBlock(summary, level) {
  const lines = [heading(level, "Does the global score mean what it says?"), ""];
  const measured = summary.calibration.filter((bucket) => bucket.records > 0);
  if (measured.length === 0) return "";
  lines.push("The similarity map turns a probability into the precision the model had at that probability on");
  lines.push("the public validation split. Measuring it on contributed audio tests whether that promise holds");
  lines.push("outside the public datasets.");
  lines.push("");
  lines.push(table(
    ["accepted score", "records", "mean score", "measured precision"],
    measured.map((bucket) => [
      `${bucket.from.toFixed(2)}–${bucket.to.toFixed(2)}`,
      bucket.records,
      bucket.meanScore.toFixed(3),
      pct(bucket.precision),
    ]),
  ));
  lines.push("");
  return lines.join("\n");
}

function mistakesBlock(summary, level) {
  const mistakes = summary.confidentWrong.cold;
  if (mistakes.length === 0) return "";
  const lines = [heading(level, "The confident mistakes, cold start"), ""];
  lines.push(table(
    ["true label", "answered as", "records"],
    mistakes.map((entry) => [`\`${entry.truth}\``, `\`${entry.predicted}\``, entry.count]),
  ));
  lines.push("");
  return lines.join("\n");
}

function policyBlock(summary, modelOnly, level) {
  if (modelOnly == null || modelOnly.standard == null) return "";
  const merged = summary.standard.app;
  const model = modelOnly.standard.app;
  if (merged.records === 0) return "";
  const localWins = summary.installs.reduce((total, install) => total + install.appLocalWins, 0);
  const lines = [heading(level, "The merge policy"), ""];
  lines.push("Since the schema-2 fix the app answers a query with the shipped model alone when the model is");
  lines.push("loaded: a raw local cosine is no longer compared with a calibrated probability. The table holds the");
  lines.push("same test rows under both rules, so the change is measured rather than described.");
  lines.push("");
  lines.push(table(
    ["rule for a schema-2 query", "records", "top-1", "coverage", "precision at acceptance (95% CI)"],
    [
      [
        "local cosine merged (previous)",
        merged.records, pct(merged.top1), pct(merged.coverage),
        `${pct(merged.precision)} (${interval(merged.precisionInterval)})`,
      ],
      [
        "shipped model alone (current)",
        model.records, pct(model.top1), pct(model.coverage),
        `${pct(model.precision)} (${interval(model.precisionInterval)})`,
      ],
    ],
  ));
  lines.push("");
  lines.push(`The local label won **${localWins}** rows under the previous rule. A local prototype still answers`);
  lines.push("when no model is shipped; the schema-1 path is unchanged.");
  lines.push("");
  return lines.join("\n");
}

function scoreBlock(summary, data, level) {
  return [
    summaryBlock(summary, level),
    installsBlock(summary, data, level),
    localBlock(summary, level),
    labelsBlock(summary, level),
    curveBlock(summary, level),
    missingBlock(summary, level),
    customBlock(summary, level),
    calibrationBlock(summary, level),
    mistakesBlock(summary, level),
  ].filter((part) => part.length > 0).join("\n");
}

export function renderHeldOutReport({ summary, tagSummary, modelOnly = null, data, snapshot, modelPath, command }) {
  const classes = summary.dataset.classes;
  const lines = [];
  lines.push("# Held-out evaluation of the shipped type model");
  lines.push("");
  lines.push(`Model: \`${modelPath}\`, generated ${summary.generatedAt ?? "unknown"}, ` +
    `${summary.labels.length} labels on ${summary.featureCount} of 84 features.`);
  lines.push("");
  lines.push(`Test data: \`${snapshot}\` — ${data.stats.lines} records from ` +
    `${Object.keys(data.stats.exportByInstall ?? {}).length} install(s), of which ${data.stats.schema1} are ` +
    "schema-1 with the old 14 features and cannot be scored by this model.");
  lines.push("");
  lines.push("Nothing here trained, tuned or selected the model. The file is read exactly as the app reads it,");
  lines.push("and the acceptance rule is the app's own: the global score carries the 0.95 penalty, a type is");
  lines.push("published from 0.80, and a schema-2 query is answered by the model alone when it is loaded. The");
  lines.push("previous merge, where a local label won when the global best was not more than 0.02 above it, is");
  lines.push("kept as the comparison row in \"The merge policy\".");
  lines.push("");
  lines.push("The export is downloaded by `node tools/contribution-sync.mjs` and stays git-ignored; the numbers");
  lines.push("below are reproducible only against the same snapshot.");
  lines.push("");
  lines.push("## What the numbers are");
  lines.push("");
  lines.push("- **Records** count the rows a block can score. **Top-1** is the model's first answer;");
  lines.push("  **coverage** is the share whose first answer clears 0.80, and **precision** is how many of");
  lines.push("  those published answers are right. The library shows both.");
  lines.push("- **Cold start** is a person the model has never met: the shipped model alone.");
  lines.push("- **With own confirmations** adds that person's other confirmed rows as local prototypes, one");
  lines.push("  record out at a time, and applies the app's rule. Under the shipped rule the model answers on its");
  lines.push("  own; the comparison block shows what the earlier local merge did on the same rows.");
  lines.push("- **Own confirmations only** drops the model, to show what the acceptance threshold means on a");
  lines.push("  raw cosine.");
  lines.push("");
  lines.push(table(
    ["class", "records", "meaning"],
    [
      ["in-vocabulary", classes.model, `label is one of the model's ${summary.labels.length}`],
      ["missing", classes.missing, "standard type the model cannot output"],
      ["custom", classes.custom, "a person's own type name"],
      ["unknown", classes.unknown, "a name that is neither standard nor marked custom"],
    ],
  ));
  lines.push("");
  lines.push("## Confirmed types");
  lines.push("");
  lines.push("The person typed the sample's Type. This is the strongest truth the contributions carry, and the");
  lines.push("only one that can judge the type field as the library stores it.");
  lines.push("");
  lines.push(scoreBlock(summary, data, 3));
  lines.push(policyBlock(summary, modelOnly === null ? null : modelOnly.summary, 3));
  if (tagSummary !== null && tagSummary.dataset.records > 0) {
    lines.push("## Tag-derived labels");
    lines.push("");
    lines.push("A row without a Type whose tags hold exactly one shipped type name (Kick, Snare, Hat, ...) is a");
    lines.push("second, weaker test set: the person did name the sound, but not in the Type field. A row whose");
    lines.push("tags hold several type names, or none, stays out. Read this block as evidence about the sound,");
    lines.push("not about what the Type column would say.");
    lines.push("");
    lines.push(`Rows taken from tags: **${data.stats.tagDerived}**; rows left out for ambiguous or absent type tags:`);
    lines.push(`**${data.stats.tagsOnly}**.`);
    lines.push("");
    lines.push(scoreBlock(tagSummary, data, 3));
    lines.push(policyBlock(tagSummary, modelOnly === null ? null : modelOnly.tags, 3));
  }
  lines.push("## Reading the numbers");
  lines.push("");
  lines.push("- The cold start is the only number that speaks for a new person. A per-install row with few");
  lines.push("  records carries a wide interval; read the interval, not the point.");
  lines.push("- The gold sample that chose and calibrated this model is the owner's catalog, 4526 confirmed rows");
  lines.push("  in `docs/TRAINING_REPORT_V2.md`. The install whose `export rows` add up to that number is that");
  lines.push("  catalog: its rows are an in-sample fit, and only the other installs measure generalization.");
  lines.push("- Local prototypes come from contributed rows, so a wrong local answer can be a wrong older");
  lines.push("  confirmation, not a model failure. Copies of the same audio are skipped as prototypes, so the");
  lines.push("  local numbers do not count a duplicate of the tested sound as its own second opinion.");
  lines.push("");
  lines.push("## Reproducing");
  lines.push("");
  lines.push("```bash");
  lines.push(command);
  lines.push("```");
  lines.push("");
  return lines.join("\n");
}
