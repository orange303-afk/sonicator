// The stage-5 report. Everything printed here comes from numbers computed in train.mjs; nothing in
// this file reads the catalog, so no path or file name can leak into the committed document.

function percent(value) {
  return `${(value * 100).toFixed(1)}%`;
}

function points(value) {
  return `${value >= 0 ? "+" : ""}${(value * 100).toFixed(1)} п.п.`;
}

function criteriaTable(criteria) {
  const rows = criteria.map((entry) => {
    const value = typeof entry.value !== "number" ? String(entry.value)
      : entry.unit === "pp" ? points(entry.value) : percent(entry.value);
    const threshold = typeof entry.threshold !== "number" ? String(entry.threshold)
      : entry.unit === "pp" ? points(entry.threshold) : percent(entry.threshold);
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
  lines.push(`Строк публичных данных: **${report.rows.public}**.`, "");
  lines.push("Золотая выборка — подтверждённые записи `user` и `approved` каталога владельца; пути и имена файлов",
    "в неё не входят, аудио не покидает машину.", "");
  if (report.rows.exported !== undefined) {
    const custom = report.rows.exported - report.rows.standard;
    lines.push(`- Экспортировано подтверждённых строк: **${report.rows.exported}**`);
    lines.push(`- Кастомный тип (в метрики по стандартным типам не входит): **${custom}**`);
    lines.push(`- Стандартных строк: **${report.rows.standard}**`);
    lines.push(`- Различных по содержимому (\`recordId\`): **${report.rows.gold}**`
      + `, отброшено дубликатов: ${report.rows.duplicates}, групп с конфликтующими метками: ${report.rows.conflicts}`);
    lines.push("");
  }

  lines.push("## Сравнения на золотой выборке", "", comparisonTable(report.comparisons), "");
  const mode4 = report.comparisons.find((entry) => entry.mode === 4);
  const mode3 = report.comparisons.find((entry) => entry.mode === 3);
  if (mode4 !== undefined && mode4.globalWins === 0) {
    lines.push(`Публичная модель решила **0** из **${mode4.records}** строк режима (4): её калиброванная`,
      "вероятность упирается в точность на `val`, а локальный косинус на сырых признаках почти всегда",
      "выше. Режим (4) на этой выборке читается как «локальный v2 с включённой публичной моделью», а не",
      "как прирост от публичных данных.", "");
  } else if (mode4 !== undefined && mode4.globalWins !== undefined) {
    lines.push(`Публичная модель решила **${mode4.globalWins}** из **${mode4.records}** строк режима (4),`,
      "остальные — локальная часть. Обе шкалы переведены в оценку точности, поэтому выбор между ними",
      "осмысленный; там, где публичная модель уступает локальной (часть `fx`), решение остаётся локальным.", "");
  }
  if (mode3 !== undefined && (mode3.missingLabels ?? []).length > 0) {
    lines.push(`Меток нет в публичных данных, поэтому режим (3) не может их угадать: ${mode3.missingLabels.join(", ")}.`
      + " Они входят в режим (4) и в знаменатель его метрик.", "");
  }

  const groupNames = [...new Set((report.comparisons ?? [])
    .flatMap((entry) => (entry.perGroup ?? []).map((group) => group.group)))].sort();
  if (groupNames.length > 0) {
    lines.push("## По группам (top-1)", "",
      "| группа | (1) v1 локальный | (2) v2 локальный | (3) публичная | (4) публичная + локальная |",
      "|---|---|---|---|---|");
    const at = (entry, group) => (entry.perGroup ?? []).find((item) => item.group === group)?.top1;
    for (const group of groupNames) {
      const cells = [1, 2, 3, 4].map((mode) => {
        const entry = report.comparisons.find((item) => item.mode === mode);
        const value = entry === undefined ? undefined : at(entry, group);
        return value === undefined ? "—" : percent(value);
      });
      lines.push(`| ${group} | ${cells.join(" | ")} |`);
    }
    lines.push("");
  }

  if (report.curves !== undefined) {
    lines.push("## Порог приёма", "",
      "Порог 0.80 применяется к оценке точности, а не к сырому косинусу: обе шкалы (локальный косинус и",
      "вероятность публичной модели) переведены в «какую точность они на самом деле несут». Поэтому",
      "покрытие ниже 100%, а точность при приёме что-то значит.", "",
      "| порог | (1) покрытие | (1) точность | (4) покрытие | (4) точность |",
      "|---|---|---|---|---|");
    for (let i = 0; i < report.curves.one.length; i += 1) {
      const one = report.curves.one[i];
      const four = report.curves.four[i];
      lines.push(`| ${one.threshold.toFixed(2)} | ${percent(one.coverage)} | ${percent(one.precision)} `
        + `| ${percent(four.coverage)} | ${percent(four.precision)} |`);
    }
    lines.push("");
  }

  if (report.choice !== undefined) {
    lines.push("## Выбор модели", "",
      `M1 (центроиды): top-1 ${percent(report.choice.m1)}; M2 (softmax): top-1 ${percent(report.choice.m2)}.`,
      `Поставлен **${report.choice.chosen}** (M2 нужен перевес не меньше 2 пунктов на холодном старте).`, "");
  }

  lines.push("## Критерии приёмки (7.6)", "", criteriaTable(report.criteria), "");
  if (report.groupNote !== undefined) lines.push(`Группы: ${report.groupNote}.`, "");
  lines.push("Набор групп признаков и модель выбирались по этой же золотой выборке, поэтому её цифры",
    "включают эффект отбора. Абсолютные значения — не независимая оценка, а результат выбора.", "");

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
    lines.push("## Матрицы ошибок", "",
      "Режим (4), строки — подтверждённая метка, столбцы — решение; «прочее» — метка вне набора.", "");
    for (const [title, block] of Object.entries(report.confusion)) lines.push(confusionBlock(title, block));
  }

  if (report.worst !== undefined && report.worst.length > 0) {
    lines.push("## Худшие 10 меток", "",
      "Режим (4); метки, у которых в золотой выборке не меньше 10 строк.", "",
      "| метка | строк | top-1 |", "|---|---|---|");
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
      `- Разбиения без утечки: NSynth официальные train/valid/test (val и test делят инструменты);`,
      `  Freesound по автору; FSD50K dev/eval.`,
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
