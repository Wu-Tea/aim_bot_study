(() => {
  "use strict";

  const spec = window.QUESTIONNAIRE_SPEC;
  if (!spec || !Array.isArray(spec.sections)) {
    document.body.innerHTML = "<p style='padding:2rem'>问题库加载失败，请确认 questions.js 与 index.html 位于同一目录。</p>";
    return;
  }

  const STORAGE_KEY = "aim-product-questionnaire/v1/state";
  const REVIEW_INDEX = spec.sections.length;
  const TOTAL_STEPS = spec.sections.length + 1;

  const elements = {
    welcome: document.querySelector("#welcome"),
    workspace: document.querySelector("#workspace"),
    startButton: document.querySelector("#start-button"),
    resumeButton: document.querySelector("#resume-button"),
    welcomeSectionCount: document.querySelector("#welcome-section-count"),
    welcomeCoreCount: document.querySelector("#welcome-core-count"),
    sectionNav: document.querySelector("#section-nav"),
    sectionView: document.querySelector("#section-view"),
    progressLabel: document.querySelector("#progress-label"),
    progressBar: document.querySelector("#progress-bar"),
    saveStatus: document.querySelector("#save-status"),
    previousButton: document.querySelector("#previous-button"),
    nextButton: document.querySelector("#next-button"),
    sectionPosition: document.querySelector("#section-position"),
    sectionResolution: document.querySelector("#section-resolution"),
    openTools: document.querySelector("#open-tools"),
    toolsDialog: document.querySelector("#tools-dialog"),
    exportJson: document.querySelector("#export-json"),
    exportMarkdown: document.querySelector("#export-markdown"),
    copyMarkdown: document.querySelector("#copy-markdown"),
    importJson: document.querySelector("#import-json"),
    importFile: document.querySelector("#import-file"),
    resetButton: document.querySelector("#reset-button"),
    resetDialog: document.querySelector("#reset-dialog"),
    confirmReset: document.querySelector("#confirm-reset"),
    toolFeedback: document.querySelector("#tool-feedback"),
    toast: document.querySelector("#toast")
  };

  const questionById = new Map();
  const sectionByQuestionId = new Map();
  const globalQuestionNumber = new Map();
  const dependentQuestionIds = new Set();
  let questionCounter = 0;

  spec.sections.forEach((section, sectionIndex) => {
    section.questions.forEach((question) => {
      questionById.set(question.id, question);
      sectionByQuestionId.set(question.id, sectionIndex);
      globalQuestionNumber.set(question.id, ++questionCounter);
      if (question.dependsOn?.id) dependentQuestionIds.add(question.dependsOn.id);
    });
  });

  const defaultState = () => ({
    currentSection: 0,
    answers: {},
    deepOpen: {},
    savedAt: null
  });

  let state = loadState();
  let active = false;
  let validationIds = new Set();
  let saveTimer = null;
  let toastTimer = null;

  function loadState() {
    try {
      const saved = JSON.parse(localStorage.getItem(STORAGE_KEY));
      if (!saved || typeof saved !== "object") return defaultState();
      return {
        ...defaultState(),
        ...saved,
        currentSection: Number.isInteger(saved.currentSection)
          ? Math.min(Math.max(saved.currentSection, 0), REVIEW_INDEX)
          : 0,
        answers: saved.answers && typeof saved.answers === "object" ? saved.answers : {},
        deepOpen: saved.deepOpen && typeof saved.deepOpen === "object" ? saved.deepOpen : {}
      };
    } catch (error) {
      return defaultState();
    }
  }

  function saveState({ announce = true } = {}) {
    state.savedAt = new Date().toISOString();
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
      if (!announce) return;
      elements.saveStatus.textContent = "正在保存…";
      window.clearTimeout(saveTimer);
      saveTimer = window.setTimeout(() => {
        elements.saveStatus.textContent = "已自动保存";
      }, 420);
    } catch (error) {
      elements.saveStatus.textContent = "自动保存失败，请立即导出";
    }
  }

  function hasAnyProgress() {
    return Object.values(state.answers).some((answer) => {
      if (!answer) return false;
      return hasValue(answer.value) || Boolean(answer.note?.trim()) || Boolean(answer.discussion);
    });
  }

  function hasValue(value) {
    if (Array.isArray(value)) return value.length > 0;
    if (typeof value === "number") return Number.isFinite(value);
    return typeof value === "string" ? value.trim().length > 0 : value !== undefined && value !== null;
  }

  function answerFor(questionId) {
    return state.answers[questionId] || { value: questionById.get(questionId)?.type === "multi" ? [] : "", note: "", discussion: false };
  }

  function ensureAnswer(questionId) {
    if (!state.answers[questionId]) {
      state.answers[questionId] = {
        value: questionById.get(questionId)?.type === "multi" ? [] : "",
        note: "",
        discussion: false
      };
    }
    return state.answers[questionId];
  }

  function isVisible(question) {
    const condition = question.dependsOn;
    if (!condition) return true;
    const value = answerFor(condition.id).value;
    if (Object.prototype.hasOwnProperty.call(condition, "equals") && value !== condition.equals) return false;
    if (Object.prototype.hasOwnProperty.call(condition, "notEquals") && value === condition.notEquals) return false;
    if (Object.prototype.hasOwnProperty.call(condition, "includes")) {
      if (!Array.isArray(value) || !value.includes(condition.includes)) return false;
    }
    if (Object.prototype.hasOwnProperty.call(condition, "notIncludes")) {
      if (Array.isArray(value) && value.includes(condition.notIncludes)) return false;
    }
    return true;
  }

  function isResolved(question) {
    const answer = answerFor(question.id);
    return Boolean(answer.discussion) || hasValue(answer.value);
  }

  function visibleQuestions(section) {
    return section.questions.filter(isVisible);
  }

  function coreQuestions(section) {
    return visibleQuestions(section).filter((question) => question.depth === "core" && question.required);
  }

  function allApplicableQuestions() {
    return spec.sections.flatMap((section) => visibleQuestions(section));
  }

  function allApplicableCore() {
    return allApplicableQuestions().filter((question) => question.depth === "core" && question.required);
  }

  function completionStats() {
    const core = allApplicableCore();
    const resolvedCore = core.filter(isResolved);
    const applicable = allApplicableQuestions();
    const deep = applicable.filter((question) => question.depth === "detail");
    const discussion = applicable.filter((question) => answerFor(question.id).discussion);
    return {
      coreTotal: core.length,
      coreResolved: resolvedCore.length,
      coreUnresolved: core.filter((question) => !isResolved(question)),
      deepTotal: deep.length,
      deepAnswered: deep.filter((question) => hasValue(answerFor(question.id).value) || Boolean(answerFor(question.id).note?.trim())).length,
      discussion,
      percent: core.length ? Math.round((resolvedCore.length / core.length) * 100) : 100
    };
  }

  function escapeHtml(value) {
    return String(value ?? "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;")
      .replaceAll("'", "&#039;");
  }

  function pad(value) {
    return String(value).padStart(2, "0");
  }

  function showWelcome() {
    active = false;
    elements.workspace.hidden = true;
    elements.welcome.hidden = false;
    const progress = hasAnyProgress();
    elements.resumeButton.hidden = !progress;
    elements.startButton.firstChild.textContent = progress ? "从第一节查看 " : "开始定义产品 ";
    elements.welcomeSectionCount.textContent = spec.sections.length;
    elements.welcomeCoreCount.textContent = spec.sections
      .flatMap((section) => section.questions)
      .filter((question) => question.depth === "core" && question.required).length;
  }

  function enterWorkspace({ resume = false } = {}) {
    active = true;
    if (!resume) state.currentSection = 0;
    elements.welcome.hidden = true;
    elements.workspace.hidden = false;
    saveState({ announce: false });
    renderCurrent();
    document.querySelector("#main")?.focus({ preventScroll: true });
  }

  function renderCurrent({ preserveScroll = false } = {}) {
    if (!active) return;
    const scrollY = window.scrollY;
    renderNavigation();
    if (state.currentSection === REVIEW_INDEX) renderReview();
    else renderSection(spec.sections[state.currentSection]);
    updateChrome();
    if (preserveScroll) window.scrollTo({ top: scrollY, behavior: "instant" });
    else window.scrollTo({ top: 0, behavior: "instant" });
  }

  function renderNavigation() {
    const items = spec.sections.map((section, index) => {
      const core = coreQuestions(section);
      const resolved = core.filter(isResolved).length;
      const complete = core.length === resolved;
      return `
        <button class="section-nav__item" type="button" data-section-index="${index}"
          ${state.currentSection === index ? 'aria-current="step"' : ""}>
          <span class="section-nav__number">${pad(index + 1)}</span>
          <span class="section-nav__title">${escapeHtml(section.title)}</span>
          <span class="section-nav__state ${complete ? "is-complete" : ""}" aria-label="${complete ? "已处理" : `${resolved}/${core.length}`}"
            title="${complete ? "本节核心问题已处理" : `本节已处理 ${resolved}/${core.length}`}" aria-hidden="true"></span>
        </button>`;
    });
    items.push(`
      <button class="section-nav__item" type="button" data-section-index="${REVIEW_INDEX}"
        ${state.currentSection === REVIEW_INDEX ? 'aria-current="step"' : ""}>
        <span class="section-nav__number">✓</span>
        <span class="section-nav__title">最终复核</span>
        <span class="section-nav__state" aria-hidden="true"></span>
      </button>`);
    elements.sectionNav.innerHTML = items.join("");
  }

  function renderSection(section) {
    const core = visibleQuestions(section).filter((question) => question.depth === "core");
    const deep = visibleQuestions(section).filter((question) => question.depth === "detail");
    const unresolved = core.filter((question) => question.required && !isResolved(question));
    const banner = validationIds.size
      ? `<div class="validation-banner" role="alert">本节还有 ${unresolved.length} 个核心问题未处理。请选择答案，或标记“需要讨论”后继续。</div>`
      : "";
    const sectionIndex = spec.sections.indexOf(section);
    const deepOpen = Boolean(state.deepOpen[section.id]);

    elements.sectionView.innerHTML = `
      <header class="section-header">
        <div>
          <p class="eyebrow">${escapeHtml(section.kicker)}</p>
          <h1>${escapeHtml(section.title)}</h1>
          <p>${escapeHtml(section.description)}</p>
        </div>
        <span class="section-header__counter">SECTION ${pad(sectionIndex + 1)}</span>
      </header>
      ${banner}
      <div class="question-list">
        ${core.map(renderQuestion).join("")}
      </div>
      ${deep.length ? `
        <details class="deep-dive" data-deep-section="${escapeHtml(section.id)}" ${deepOpen ? "open" : ""}>
          <summary>
            <span>
              <span class="deep-dive__title">深入细节 · ${deep.length} 个非阻塞问题</span>
              <span class="deep-dive__description">现在不确定可以先跳过；填写后会一起导出供 review。</span>
            </span>
            <span class="deep-dive__icon" aria-hidden="true">＋</span>
          </summary>
          <div class="question-list">${deep.map(renderQuestion).join("")}</div>
        </details>` : ""}`;
  }

  function renderQuestion(question) {
    const answer = answerFor(question.id);
    const number = globalQuestionNumber.get(question.id);
    const invalid = validationIds.has(question.id);
    const badges = [
      question.depth === "core" ? '<span class="badge badge--core">核心决策</span>' : '<span class="badge">深入细节</span>',
      answer.discussion ? '<span class="badge">待讨论</span>' : ""
    ].join("");
    const context = question.context ? `<p class="question__context">${escapeHtml(question.context)}</p>` : "";
    const input = renderInput(question);
    const noteOpen = Boolean(answer.note?.trim());

    return `
      <article class="question ${invalid ? "is-invalid" : ""}" id="question-${escapeHtml(question.id)}" data-question-id="${escapeHtml(question.id)}">
        <div class="question__meta">
          <span class="question__number">Q${pad(number)}</span>
          <span class="question__badges">${badges}</span>
        </div>
        ${input}
        ${context}
        ${renderControl(question, answer)}
        <div class="question__footer">
          <button class="discussion-toggle" type="button" data-discussion="${escapeHtml(question.id)}" aria-pressed="${answer.discussion ? "true" : "false"}">
            <span class="discussion-toggle__dot" aria-hidden="true"></span>
            ${answer.discussion ? "已标记：需要讨论" : "需要讨论"}
          </button>
          <details class="note-details" ${noteOpen ? "open" : ""}>
            <summary>补充说明${noteOpen ? " · 已填写" : ""}</summary>
            <textarea class="note-input" data-note="${escapeHtml(question.id)}" aria-label="${escapeHtml(question.title)}的补充说明" placeholder="为什么这样选、有什么例外，或还需要什么证据……">${escapeHtml(answer.note || "")}</textarea>
          </details>
        </div>
      </article>`;
  }

  function renderInput(question) {
    return `<h2 id="question-title-${escapeHtml(question.id)}">${escapeHtml(question.title)}</h2>`;
  }

  function renderControl(question, answer) {
    if (question.type === "single" || question.type === "multi") {
      const inputType = question.type === "single" ? "radio" : "checkbox";
      const selected = question.type === "multi"
        ? (Array.isArray(answer.value) ? answer.value : [])
        : [answer.value];
      return `<div class="options" role="${question.type === "single" ? "radiogroup" : "group"}" aria-labelledby="question-title-${escapeHtml(question.id)}">
        ${(question.options || []).map((item) => `
          <label class="option">
            <input type="${inputType}" name="answer-${escapeHtml(question.id)}" value="${escapeHtml(item.value)}"
              data-answer="${escapeHtml(question.id)}" ${selected.includes(item.value) ? "checked" : ""}>
            <span class="option__copy">
              <span class="option__label">${escapeHtml(item.label)}</span>
              ${item.description ? `<span class="option__description">${escapeHtml(item.description)}</span>` : ""}
            </span>
            ${item.tag ? `<span class="option__tag">${escapeHtml(item.tag)}</span>` : ""}
          </label>`).join("")}
      </div>`;
    }

    const common = `data-answer="${escapeHtml(question.id)}" aria-labelledby="question-title-${escapeHtml(question.id)}" placeholder="${escapeHtml(question.placeholder || "")}"`;
    if (question.type === "textarea") {
      return `<textarea class="text-input" ${common} rows="5">${escapeHtml(answer.value || "")}</textarea>`;
    }
    if (question.type === "number") {
      return `<div class="number-wrap">
        <input class="number-input" type="number" ${common} value="${escapeHtml(answer.value ?? "")}"
          ${question.min !== undefined ? `min="${question.min}"` : ""} ${question.max !== undefined ? `max="${question.max}"` : ""}>
        ${question.unit ? `<span class="number-unit">${escapeHtml(question.unit)}</span>` : ""}
      </div>`;
    }
    return `<input class="text-input" type="text" ${common} value="${escapeHtml(answer.value || "")}">`;
  }

  function renderReview() {
    const stats = completionStats();
    const discussionItems = stats.discussion;
    const deepMissing = allApplicableQuestions().filter((question) => question.depth === "detail" && !hasValue(answerFor(question.id).value) && !answerFor(question.id).discussion);
    const completeMessage = stats.coreUnresolved.length === 0
      ? "所有适用的核心问题都已经处理。现在可以导出 JSON 给我 review；待讨论项会被完整保留。"
      : `还有 ${stats.coreUnresolved.length} 个核心问题没有答案，也没有标记“需要讨论”。你仍然可以导出，但 review 会先指出这些缺口。`;

    elements.sectionView.innerHTML = `
      <header class="section-header">
        <div>
          <p class="eyebrow">${pad(REVIEW_INDEX + 1)} / REVIEW</p>
          <h1>最终复核</h1>
          <p>${escapeHtml(completeMessage)}</p>
        </div>
        <span class="section-header__counter">READY FOR REVIEW</span>
      </header>
      <div class="review-summary">
        <div class="review-stat"><strong>${stats.coreResolved}/${stats.coreTotal}</strong><span>核心问题已处理</span></div>
        <div class="review-stat"><strong>${discussionItems.length}</strong><span>需要继续讨论</span></div>
        <div class="review-stat"><strong>${stats.deepAnswered}/${stats.deepTotal}</strong><span>深入细节已填写</span></div>
      </div>
      ${renderReviewGroup("未处理的核心问题", stats.coreUnresolved, "这些问题会影响产品规格，建议填写或明确标成待讨论。")}
      ${renderReviewGroup("已标记需要讨论", discussionItems, "导出后我会逐项 review，不会把临时答案误当成最终决定。")}
      ${renderReviewGroup("尚未填写的深入细节", deepMissing, "这些问题不阻塞当前进度，可以在 review 后按需要补充。", true)}
      <div class="review-actions">
        <button class="button button--primary" type="button" data-review-action="json">导出 JSON 给 Codex</button>
        <button class="button button--secondary" type="button" data-review-action="markdown">导出 Markdown 备份</button>
        <button class="button button--secondary" type="button" data-review-action="tools">打开全部工具</button>
      </div>`;
  }

  function renderReviewGroup(title, questions, description, collapsed = false) {
    if (!questions.length) {
      if (title !== "未处理的核心问题") return "";
      return `<section class="review-group"><h2>${escapeHtml(title)}</h2><p class="question__context">没有缺项。</p></section>`;
    }
    const list = questions.map((question) => {
      const sectionIndex = sectionByQuestionId.get(question.id);
      return `<li class="review-item">
        <span class="review-item__copy">
          <strong>${escapeHtml(question.title)}</strong>
          <span>${pad(sectionIndex + 1)} · ${escapeHtml(spec.sections[sectionIndex].title)}</span>
        </span>
        <button class="jump-button" type="button" data-jump-section="${sectionIndex}" data-jump-question="${escapeHtml(question.id)}">回到问题</button>
      </li>`;
    }).join("");
    const body = `<p class="question__context">${escapeHtml(description)}</p><ul class="review-list">${list}</ul>`;
    if (collapsed) {
      return `<details class="deep-dive review-group"><summary><span><span class="deep-dive__title">${escapeHtml(title)} · ${questions.length}</span><span class="deep-dive__description">${escapeHtml(description)}</span></span><span class="deep-dive__icon" aria-hidden="true">＋</span></summary><ul class="review-list">${list}</ul></details>`;
    }
    return `<section class="review-group"><h2>${escapeHtml(title)} · ${questions.length}</h2>${body}</section>`;
  }

  function updateChrome() {
    const stats = completionStats();
    elements.progressLabel.textContent = `${stats.percent}%`;
    elements.progressBar.style.width = `${stats.percent}%`;
    elements.previousButton.disabled = state.currentSection === 0;
    elements.sectionPosition.textContent = `${pad(state.currentSection + 1)} / ${pad(TOTAL_STEPS)}`;

    if (state.currentSection === REVIEW_INDEX) {
      elements.nextButton.innerHTML = '导出 JSON 给 Codex <span aria-hidden="true">↓</span>';
      elements.sectionResolution.textContent = `总体 ${stats.coreResolved} / ${stats.coreTotal}`;
      return;
    }

    const section = spec.sections[state.currentSection];
    const core = coreQuestions(section);
    const resolved = core.filter(isResolved).length;
    elements.sectionResolution.textContent = `本节 ${resolved} / ${core.length}`;
    elements.nextButton.innerHTML = state.currentSection === spec.sections.length - 1
      ? '进入最终复核 <span aria-hidden="true">→</span>'
      : '保存并继续 <span aria-hidden="true">→</span>';
  }

  function goToSection(index, questionId = null) {
    state.currentSection = Math.min(Math.max(index, 0), REVIEW_INDEX);
    validationIds.clear();
    saveState();
    renderCurrent();
    if (questionId) {
      window.setTimeout(() => {
        const target = document.querySelector(`#question-${CSS.escape(questionId)}`);
        target?.scrollIntoView({ behavior: "smooth", block: "start" });
        target?.querySelector("input, textarea, button")?.focus({ preventScroll: true });
      }, 80);
    }
  }

  function handleNext() {
    if (state.currentSection === REVIEW_INDEX) {
      exportJsonFile();
      return;
    }
    const section = spec.sections[state.currentSection];
    const unresolved = coreQuestions(section).filter((question) => !isResolved(question));
    if (unresolved.length) {
      validationIds = new Set(unresolved.map((question) => question.id));
      renderCurrent({ preserveScroll: true });
      window.setTimeout(() => {
        const first = document.querySelector(`#question-${CSS.escape(unresolved[0].id)}`);
        first?.scrollIntoView({ behavior: "smooth", block: "center" });
        first?.querySelector("input, textarea, button")?.focus({ preventScroll: true });
      }, 50);
      return;
    }
    goToSection(state.currentSection + 1);
  }

  function clearValidation(questionId) {
    if (!validationIds.has(questionId)) return;
    if (isResolved(questionById.get(questionId))) {
      validationIds.delete(questionId);
      document.querySelector(`#question-${CSS.escape(questionId)}`)?.classList.remove("is-invalid");
      if (!validationIds.size) document.querySelector(".validation-banner")?.remove();
    }
  }

  function handleAnswerInput(target) {
    const questionId = target.dataset.answer;
    const question = questionById.get(questionId);
    if (!question) return;
    const answer = ensureAnswer(questionId);

    if (question.type === "multi") {
      let selected = [...document.querySelectorAll(`input[data-answer="${CSS.escape(questionId)}"]:checked`)].map((input) => input.value);
      const clickedOption = question.options?.find((item) => item.value === target.value);
      const exclusiveValues = new Set((question.options || []).filter((item) => item.exclusive).map((item) => item.value));
      if (target.checked && clickedOption?.exclusive) {
        selected = [target.value];
      } else if (target.checked) {
        selected = selected.filter((value) => !exclusiveValues.has(value));
      }
      answer.value = selected;
      document.querySelectorAll(`input[data-answer="${CSS.escape(questionId)}"]`).forEach((input) => {
        input.checked = selected.includes(input.value);
      });
    } else if (question.type === "number") {
      answer.value = target.value === "" ? "" : Number(target.value);
    } else {
      answer.value = target.value;
    }

    clearValidation(questionId);
    saveState();
    if ((question.type === "single" || question.type === "multi") && dependentQuestionIds.has(questionId)) {
      renderCurrent({ preserveScroll: true });
    } else {
      renderNavigation();
      updateChrome();
    }
  }

  function createExportData() {
    const stats = completionStats();
    const responses = [];

    spec.sections.forEach((section, sectionIndex) => {
      section.questions.forEach((question) => {
        const answer = answerFor(question.id);
        const applicable = isVisible(question);
        const values = Array.isArray(answer.value) ? answer.value : hasValue(answer.value) ? [answer.value] : [];
        const labels = (question.options || [])
          .filter((item) => values.includes(item.value))
          .map((item) => item.label);
        responses.push({
          section_id: section.id,
          section_title: section.title,
          section_number: sectionIndex + 1,
          question_id: question.id,
          question_number: globalQuestionNumber.get(question.id),
          depth: question.depth,
          required: Boolean(question.required),
          applicable,
          question: question.title,
          context: question.context || "",
          answer: {
            value: answer.value ?? (question.type === "multi" ? [] : ""),
            labels
          },
          note: answer.note || "",
          needs_discussion: Boolean(answer.discussion),
          resolved: applicable ? isResolved(question) : null
        });
      });
    });

    return {
      schema_version: spec.schemaVersion,
      generated_at: new Date().toISOString(),
      project: spec.project,
      title: spec.title,
      purpose: "产品需求、行为边界、验收标准与 benchmark 仿真需求的决策输入",
      review_request: "请先 review 产品定义中的缺口、冲突和风险；确认后再制定 refactor 与 benchmark 实施计划。",
      principles: spec.principles,
      summary: {
        section_count: spec.sections.length,
        core_total: stats.coreTotal,
        core_resolved: stats.coreResolved,
        core_unresolved: stats.coreUnresolved.length,
        needs_discussion: stats.discussion.length,
        detail_total: stats.deepTotal,
        detail_answered: stats.deepAnswered,
        completion_percent: stats.percent
      },
      responses
    };
  }

  function labelForAnswer(question, answer) {
    if (!hasValue(answer.value)) return "未填写";
    if (!question.options) return String(answer.value);
    const values = Array.isArray(answer.value) ? answer.value : [answer.value];
    const labels = question.options.filter((item) => values.includes(item.value)).map((item) => item.label);
    return labels.length ? labels.join("；") : values.join("；");
  }

  function createMarkdown() {
    const stats = completionStats();
    const lines = [
      `# ${spec.title}回答`,
      "",
      `- 项目：${spec.project}`,
      `- 导出时间：${new Date().toLocaleString("zh-CN", { hour12: false })}`,
      `- 核心问题：${stats.coreResolved}/${stats.coreTotal} 已处理`,
      `- 待讨论：${stats.discussion.length}`,
      `- 深入细节：${stats.deepAnswered}/${stats.deepTotal} 已填写`,
      "",
      "> 请先 review 产品定义中的缺口、冲突和风险；确认后再制定 refactor 与 benchmark 实施计划。",
      ""
    ];

    spec.sections.forEach((section, sectionIndex) => {
      lines.push(`## ${sectionIndex + 1}. ${section.title}`, "");
      section.questions.filter(isVisible).forEach((question) => {
        const answer = answerFor(question.id);
        lines.push(`### Q${pad(globalQuestionNumber.get(question.id))} ${question.title}`);
        lines.push(`- 层级：${question.depth === "core" ? "核心决策" : "深入细节"}`);
        lines.push(`- 答案：${labelForAnswer(question, answer).replaceAll("\n", "  \n")}`);
        lines.push(`- 状态：${answer.discussion ? "需要讨论" : isResolved(question) ? "已回答" : "未回答"}`);
        if (answer.note?.trim()) lines.push(`- 补充：${answer.note.trim().replaceAll("\n", "  \n")}`);
        lines.push("");
      });
    });
    return lines.join("\n");
  }

  function filenameStamp() {
    const date = new Date();
    const parts = [
      date.getFullYear(),
      pad(date.getMonth() + 1),
      pad(date.getDate()),
      "-",
      pad(date.getHours()),
      pad(date.getMinutes())
    ];
    return parts.join("");
  }

  function downloadText(content, filename, mimeType) {
    const blob = new Blob([content], { type: `${mimeType};charset=utf-8` });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  function exportJsonFile() {
    downloadText(JSON.stringify(createExportData(), null, 2), `产品定义问卷-${filenameStamp()}.json`, "application/json");
    showToast("JSON 已导出。把文件发回当前任务，我会先做产品 review。 ");
    elements.toolFeedback.textContent = "JSON 已导出，可直接交给 Codex review。";
  }

  function exportMarkdownFile() {
    downloadText(createMarkdown(), `产品定义问卷-${filenameStamp()}.md`, "text/markdown");
    showToast("Markdown 备份已导出。 ");
    elements.toolFeedback.textContent = "Markdown 备份已导出。";
  }

  async function copyMarkdownToClipboard() {
    const markdown = createMarkdown();
    try {
      if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(markdown);
      } else {
        const textarea = document.createElement("textarea");
        textarea.value = markdown;
        textarea.style.position = "fixed";
        textarea.style.opacity = "0";
        document.body.appendChild(textarea);
        textarea.select();
        document.execCommand("copy");
        textarea.remove();
      }
      elements.toolFeedback.textContent = "Markdown 已复制到剪贴板。";
      showToast("已复制 Markdown。 ");
    } catch (error) {
      elements.toolFeedback.textContent = "复制失败，请改用“导出 Markdown”。";
    }
  }

  function importExportedData(data) {
    if (!data || data.schema_version !== spec.schemaVersion || !Array.isArray(data.responses)) {
      throw new Error(`文件格式不匹配，需要 ${spec.schemaVersion}`);
    }
    const importedAnswers = {};
    data.responses.forEach((response) => {
      const question = questionById.get(response.question_id);
      if (!question) return;
      let value = response.answer?.value;
      if (question.type === "multi" && !Array.isArray(value)) value = hasValue(value) ? [value] : [];
      if (question.type !== "multi" && Array.isArray(value)) value = value[0] ?? "";
      importedAnswers[question.id] = {
        value: value ?? (question.type === "multi" ? [] : ""),
        note: typeof response.note === "string" ? response.note : "",
        discussion: Boolean(response.needs_discussion)
      };
    });
    state.answers = importedAnswers;
    state.currentSection = 0;
    state.deepOpen = {};
    validationIds.clear();
    saveState();
    if (active) renderCurrent();
  }

  function showToast(message) {
    elements.toast.textContent = message;
    elements.toast.hidden = false;
    window.clearTimeout(toastTimer);
    toastTimer = window.setTimeout(() => {
      elements.toast.hidden = true;
    }, 3600);
  }

  function openDialog(dialog) {
    if (typeof dialog.showModal === "function") dialog.showModal();
    else dialog.setAttribute("open", "");
  }

  elements.startButton.addEventListener("click", () => enterWorkspace({ resume: false }));
  elements.resumeButton.addEventListener("click", () => enterWorkspace({ resume: true }));
  elements.previousButton.addEventListener("click", () => goToSection(state.currentSection - 1));
  elements.nextButton.addEventListener("click", handleNext);

  elements.sectionNav.addEventListener("click", (event) => {
    const button = event.target.closest("[data-section-index]");
    if (!button) return;
    goToSection(Number(button.dataset.sectionIndex));
  });

  elements.sectionView.addEventListener("input", (event) => {
    const target = event.target;
    if (target.matches('[data-answer]:not([type="radio"]):not([type="checkbox"])')) handleAnswerInput(target);
    if (target.matches("[data-note]")) {
      ensureAnswer(target.dataset.note).note = target.value;
      saveState();
    }
  });

  elements.sectionView.addEventListener("change", (event) => {
    const target = event.target;
    if (target.matches('input[type="radio"][data-answer], input[type="checkbox"][data-answer]')) handleAnswerInput(target);
  });

  elements.sectionView.addEventListener("click", (event) => {
    const discussion = event.target.closest("[data-discussion]");
    if (discussion) {
      const questionId = discussion.dataset.discussion;
      const answer = ensureAnswer(questionId);
      answer.discussion = !answer.discussion;
      clearValidation(questionId);
      saveState();
      renderCurrent({ preserveScroll: true });
      return;
    }
    const jump = event.target.closest("[data-jump-section]");
    if (jump) {
      goToSection(Number(jump.dataset.jumpSection), jump.dataset.jumpQuestion);
      return;
    }
    const reviewAction = event.target.closest("[data-review-action]")?.dataset.reviewAction;
    if (reviewAction === "json") exportJsonFile();
    if (reviewAction === "markdown") exportMarkdownFile();
    if (reviewAction === "tools") openDialog(elements.toolsDialog);
  });

  elements.sectionView.addEventListener("toggle", (event) => {
    const details = event.target.closest("[data-deep-section]");
    if (!details || event.target !== details) return;
    state.deepOpen[details.dataset.deepSection] = details.open;
    saveState({ announce: false });
  }, true);

  elements.openTools.addEventListener("click", () => {
    elements.toolFeedback.textContent = "";
    openDialog(elements.toolsDialog);
  });
  elements.exportJson.addEventListener("click", exportJsonFile);
  elements.exportMarkdown.addEventListener("click", exportMarkdownFile);
  elements.copyMarkdown.addEventListener("click", copyMarkdownToClipboard);
  elements.importJson.addEventListener("click", () => elements.importFile.click());
  elements.importFile.addEventListener("change", async () => {
    const file = elements.importFile.files?.[0];
    if (!file) return;
    try {
      const data = JSON.parse(await file.text());
      importExportedData(data);
      elements.toolFeedback.textContent = "导入成功，答案已自动保存。";
      showToast("问卷答案已导入。 ");
    } catch (error) {
      elements.toolFeedback.textContent = `导入失败：${error.message}`;
    } finally {
      elements.importFile.value = "";
    }
  });

  elements.resetButton.addEventListener("click", () => openDialog(elements.resetDialog));
  elements.confirmReset.addEventListener("click", () => {
    localStorage.removeItem(STORAGE_KEY);
    state = defaultState();
    validationIds.clear();
    elements.resetDialog.close();
    elements.toolsDialog.close();
    showWelcome();
    showToast("本地答案已清空。 ");
  });

  showWelcome();
})();
