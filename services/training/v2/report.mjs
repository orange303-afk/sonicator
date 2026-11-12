// The stage-5 report. Everything printed here comes from numbers computed in train.mjs; nothing in
// this file reads the catalog, so no path or file name can leak into the committed document.

function percent(value) {
  return `${(value * 100).toFixed(1)}%`;
}

function criteriaTable(criteria) {
  const rows = criteria.map((entry) => {
    const value = typeof entry.value === "number" ? percent(entry.value) : String(entry.value);
    const threshold = typeof entry.threshold === "number" ? percent(entry.threshold) : String(entry.threshold);
    return `| ${entry.name} | ${value} | ${threshold} | ${entry.pass ? "✓" : "✗"} |`;
  });
  return ["| критерий | значение | порог | итог |", "|---|---|---|---|", ...rows].join("\n");
}

function comparisonTable(comparisons) {
  const rows = comparisons.map((entry) =>
    `| (${entry.mode}) ${entry.name} | ${percent(entry.top1)} | ${percent(entry.top3)} | ${percent(entry.coverage)} | ${percent(entry.precision)} | ${entry.records} |`);
  return ["| режим | top-1 | top-3 | покрытие | точность при приёме | строк |",
    "|---|---|---|---|---|---|", ...rows].join("\n");
}

function confusionBlock(title, block) {
  if (block === undefined) return "";
  const columns = block.columns ?? block.labels ?? [];
  const rowLabels = block.rows ?? block.labels ?? [];
  if (rowLabels.length === 0 || columns.length === 0) return "";
  const header = ["| истина \\ предсказано |", ...columns.map((label) => `${label} |`)].join(" ");
  const separator = ["|---|", ...columns.map(() => "---|")].join(" ");
  const rows = rowLabels.map((label, rowIndex) =>
    `| ${label} | ${block.cells[rowIndex].map((value) => value).join(" | ")} |`);
  return `### ${title}\n\n${header}\n${separator}\n${rows.join("\n")}\n`;
}

export function renderReport(report) {
  const lines = [];
  lines.push("# Отчёт этапа 5: обучение модели v2", "");
  lines.push(`Дата модели: ${report.generatedAt}`, "");
  lines.push(`Строк публичных данных: **${report.rows.public}**, строк золотой выборки: **${report.rows.gold}**.`, "");
  lines.push("Золотая выборка — подтверждённые записи `user` и `approved` каталога владельца; пути и имена файлов",
    "в неё не входят, аудио не покидает машину.", "");

  lines.push("## Сравнения на золотой выборке", "", comparisonTable(report.comparisons), "");

  if (report.choice !== undefined) {
    lines.push("## Выбор модели", "",
      `M1 (центроиды): top-1 ${percent(report.choice.m1)}; M2 (softmax): top-1 ${percent(report.choice.m2)}.`,
      `Поставлен **${report.choice.chosen}** (M2 нужен перевес не меньше 2 пунктов на холодном старте).`, "");
  }

  lines.push("## Критерии приёмки (7.6)", "", criteriaTable(report.criteria), "");

  if (report.ablation !== undefined && report.ablation.length > 0) {
    lines.push("## Абляция групп признаков", "",
      "G1 входит всегда. Группа остаётся при приросте не менее 0.5 пункта на `val` публичных данных и",
      "потере не больше 0.5 пункта на золотой выборке.", "",
      "| группа | Δ top-1 val | Δ top-1 золота | оставлена |", "|---|---|---|---|");
    for (const entry of report.ablation) {
      const delta = (value) => `${value >= 0 ? "+" : ""}${(value * 100).toFixed(1)} п.п.`;
      lines.push(`| ${entry.group} | ${delta(entry.valDelta)} | ${delta(entry.goldDelta)} | ${entry.kept ? "да" : "нет"} |`);
    }
    lines.push("", `Итоговый набор измерений: **${report.featureCount}** из 84.`, "");
  }

  if (report.confusion !== undefined) {
    lines.push("## Матрицы ошибок", "");
    for (const [title, block] of Object.entries(report.confusion)) lines.push(confusionBlock(title, block));
  }

  if (report.worst !== undefined && report.worst.length > 0) {
    lines.push("## Худшие 10 меток", "", "| метка | строк | top-1 |", "|---|---|---|");
    for (const entry of report.worst) lines.push(`| ${entry.label} | ${entry.records} | ${percent(entry.top1)} |`);
    lines.push("");
  }

  if (report.foley !== undefined && report.foley.length > 0) {
    lines.push("## FSD50K eval по группам (foley)", "",
      "Публичная модель без пользовательских данных; только метки стандартной таксономии.", "",
      "| группа | строк | top-1 |", "|---|---|---|");
    for (const entry of report.foley) lines.push(`| ${entry.group} | ${entry.records} | ${percent(entry.top1)} |`);
    lines.push("");
  }

  if (report.model !== undefined) {
    lines.push("## Файл модели", "",
      `- Модель: ${report.model.kind}`,
      `- Файл: \`${report.model.file}\``,
      `- Размер: ${report.model.bytes} байт`,
      `- \`JSON.parse\`: ≤ ${report.model.parseMs.toFixed(0)} мс`,
      `- Разбиения без утечки: NSynth официальные train/valid/test; Freesound по автору; FSD50K dev/eval.`,
      "");
    if (report.datasets !== undefined) {
      lines.push("| датасет | уровень | клипов в волне |", "|---|---|---|");
      for (const entry of report.datasets)
        lines.push(`| ${entry.id} | ${entry.tier} | ${entry.clipsUsed} |`);
      lines.push("");
    }
  }

  lines.push("## Что дальше", "",
    "Результат критериев решает владелец (ТЗ §10.3): этап 6 или сначала волна 2 датасетов.", "");
  return lines.join("\n");
}
