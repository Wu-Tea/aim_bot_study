# Control Optimization Knowledge Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Preserve the 2026-07-16 through 2026-07-20 aim-control optimization history, extract a reusable evidence-driven methodology, and compact project context without losing historical detail.

**Architecture:** Use a project-specific causal history for facts and a separate cross-project playbook for abstractions. Preserve existing context verbatim in an archive before replacing oversized startup files with short indexes that link to the archive and the two new documents.

**Tech Stack:** Markdown, Git history, fixed-seed benchmark artifacts, Agent Context Sync files.

---

### Task 1: Build the evidence map

**Files:**
- Read: `docs/project/REFACTOR_B_BASELINE_20260716.md`
- Read: `docs/project/REFACTOR_B_ACCEPTANCE_20260716.md`
- Read: `docs/project/LEGACY_AIM_FEEL_RESTORE_20260716.md`
- Read: `docs/project/CONTROLLER_CONFIG_AUDIT_20260716.md`
- Read: `docs/project/AIM_TUNING_SWEEP_20260718.md`
- Read: `docs/project/RESPONSE_MODEL_AIM_CONTROL_ACCEPTANCE_20260718.md`
- Read: `docs/project/BRAKE_EPISODE_BENCHMARK_ACCEPTANCE_20260719.md`
- Read: `docs/project/COUNTERFACTUAL_CONFLICT_BENCHMARK_ACCEPTANCE_20260719.md`
- Read: relevant specs/plans under `docs/superpowers/`
- Read: commits from 2026-07-16 through 2026-07-20

- [x] **Step 1: Extract phase evidence**

For each phase, record symptom, hypothesis, intervention, verification artifact,
measured outcome, and unresolved limitation. Prefer acceptance documents over
commit titles, and commit titles over conversational recollection.

- [x] **Step 2: Classify claims**

Tag working notes as `user-confirmed`, `repository-evidence`, or `inferred`.
Reject direct numeric comparisons when an artifact lacks configuration
provenance or uses different benchmark semantics.

### Task 2: Write the project optimization history

**Files:**
- Create: `docs/project/AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md`

- [x] **Step 1: Write the causal timeline**

Use this section structure:

```markdown
# Aim Control Optimization History: 2026-07-16 to 2026-07-20
## Executive Summary
## Starting Failure Pattern
## Phase 1 - Refactor B Removes Accidental Control Stacking
## Phase 2 - Recovering Feel Without Restoring the Gate Stack
## Phase 3 - Making Real Combat and Human Error Measurable
## Phase 4 - Sustained Tracking and Meaningful Brake Metrics
## Phase 5 - Counterfactual Local/Global Optimization
## Phase 6 - Causal Vector Fusion and Target Inertia
## Phase 7 - Runtime Contract Corrections
## Failed or Rejected Directions
## Current Architecture and Verified Baseline
## Remaining Risks and Next Evidence
## Evidence Index
```

Each phase must include the observation-to-evidence-to-change-to-result chain.

- [x] **Step 2: Record rejected interpretations**

Explicitly distinguish useful old feel from accidental duplicated force; explain
why small benchmark movement, zero overshoot, lab-only tuning, and raw proposal
conflict could each mislead optimization.

- [x] **Step 3: Link exact evidence**

Link acceptance documents, plans, relevant source files, commits, and retained
artifacts. Label historical configuration-unproven artifacts instead of using
them as authoritative baselines.

### Task 3: Write the reusable methodology and skill seed

**Files:**
- Create: `docs/project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md`

- [x] **Step 1: Define the optimization loop**

Document a repeatable loop:

```text
runtime identity -> evidence chain -> defect fixture -> metric contract ->
counterfactual A/B -> minimal policy change -> fixed-seed gate -> live smoke ->
hand-feel validation -> durable decision
```

- [x] **Step 2: Define benchmark and metric rules**

Cover realistic user mistakes, occlusion and target transitions, proposal versus
delivered output, local regret versus future burden, state-machine semantic
parity, configuration provenance, additive scoring, and anti-intervention
metrics.

- [x] **Step 3: Define architecture and complexity rules**

Explain authority-before-strength, one owner per decision, explicit lifecycle,
complexity budgets, gate deletion criteria, response learning without weapon
tables, and when to tune parameters versus redesign policy.

- [x] **Step 4: Add a skill-ready package proposal**

Specify a future `evidence-driven-control-optimization` skill with trigger
conditions, workflow, required references, expected artifacts, stop conditions,
and project-specific material that must not be generalized automatically. Do
not create or install the skill in this task.

### Task 4: Apply SyncSet sync-20260720-001

**Files:**
- Create: `.agent-context/archive/context-through-2026-07-20-pre-compaction.md`
- Rewrite: `.agent-context/session-log.md`
- Rewrite: `.agent-context/handoff.md`

- [x] **Step 1: Archive existing context verbatim**

Copy the complete pre-compaction `handoff.md` and `session-log.md` into the
archive file under clearly labeled headings before modifying either source.
Verify the archive contains both original line counts (`277` and `247` at
design time) or the current counts if they changed before execution.

- [x] **Step 2: Rewrite the primary session log**

Keep `session-log.md` as the startup entry point. Include an archive link,
compact milestone summaries, a detailed 2026-07-16 to 2026-07-20 entry, evidence
links, user-confirmed statements, inferred transferable lessons, and follow-up.
Target fewer than 160 lines.

- [x] **Step 3: Condense the handoff**

Use the required headings: Current Objective, Current State, Next Action,
Blockers, Active Questions, Relevant Decisions, Files To Read First, Do Not
Reopen Unless Needed, Notes. Include a staleness trigger and target fewer than
120 lines.

- [x] **Step 4: Verify preservation and context quality**

Confirm the archive contains the old first and last distinctive headings,
the new primary files link to the archive and both new project documents, all
inferred claims are labeled, and no personal paths or secrets were introduced.

### Task 5: Review and commit

**Files:**
- Verify all files from Tasks 2-4

- [x] **Step 1: Run structural checks**

Search for placeholder markers, verify Markdown links resolve locally, measure
line counts, and run `git diff --check`.

- [x] **Step 2: Cross-check repository state**

Ensure claims about branches, commits, capture size, ADS epoch, selector cue
authority, Autofire cadence, and background launchers match current `dev`.

- [x] **Step 3: Commit**

```powershell
git add docs/project/AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md docs/project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md .agent-context/archive/context-through-2026-07-20-pre-compaction.md .agent-context/session-log.md .agent-context/handoff.md
git commit -m "docs: preserve aim control optimization methodology"
```
