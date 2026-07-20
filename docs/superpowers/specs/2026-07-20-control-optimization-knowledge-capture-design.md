# Control Optimization Knowledge Capture Design

## Purpose

Preserve the native aim-control work from 2026-07-16 through 2026-07-20 as
both project history and reusable engineering method. The output must explain
how defects were discovered, why several plausible strategies underperformed,
how benchmark semantics evolved, and why the accepted architecture improved
control without rebuilding the old gate stack.

## Narrative Thesis

Refactor B removed duplicate gates, brakes, ownership paths, and scattered
controller stages. This made the runtime more natural and less prone to jumps,
but also removed accidental strength and damping produced by duplicated logic.
The immediate feel regression was therefore not proof that the refactor was
wrong. It exposed that the old feel depended partly on uncontrolled stacking.
Later work recovered stronger performance through better evidence, response
modeling, benchmark coverage, and explicit authority/fusion policy rather than
restoring the duplicated gates.

This thesis is user-confirmed. Exact code, benchmark, and commit statements must
remain evidence-backed. Cross-project conclusions must be labeled as inferred
or proposed methodology.

## Deliverable 1: Project Optimization History

Create `docs/project/AIM_CONTROL_OPTIMIZATION_HISTORY_20260716_20260720.md`.
It is a chronological and causal account, not a commit transcript. It covers:

1. Pre-refactor controller debt: repeated gates, overlapping brake behavior,
   hidden authority paths, stale parameters, and configuration-unproven tests.
2. Refactor B: the simplified pipeline, what was deliberately removed, the
   improved naturalness, and the lost strength/damping.
3. Recovery attempts: legacy-strength restoration, config provenance,
   geometry alignment, partial-occlusion scenarios, per-axis arbitration, and
   why small metric deltas did not prove practical benefit.
4. Benchmark evolution: real-scene fixtures, classic user mistakes, sustained
   one-minute tracking, slowdown-ring modeling, additive scoring, brake episode
   metrics, and meaningful overshoot definitions.
5. Local/global optimization: trace recording, conflict episodes,
   counterfactual branches, future burden, causal vector candidates, and target
   inertia.
6. Runtime contracts: Autofire cadence, one snap per physical ADS epoch,
   ADS-to-BodyLock semantics, selector near-target continuity, friendly hard
   filtering, yellow-cue auxiliary authority, drift-aware unreliable fallback,
   `480x416` capture/engine alignment, and background lifecycle launchers.
7. Current limitations and next evidence to collect.

Each phase states symptom, initial hypothesis, evidence, intervention, measured
effect, rejected interpretation, and durable lesson.

## Deliverable 2: Reusable Methodology

Create `docs/project/EVIDENCE_DRIVEN_REALTIME_CONTROL_OPTIMIZATION.md`. It
separates project facts from reusable practice and includes:

- runtime identity before source reasoning;
- evidence chains spanning video, logs, config, binary, model, and fixed-seed
  artifact;
- defect-first benchmark design;
- realistic human input, including drift, delay, overshoot, reversal, and
  movement-assisted correction;
- proposal versus delivered-output measurements;
- local decision regret versus global future burden;
- counterfactual replay over an identical trace;
- state-machine semantic parity between benchmark and production;
- authority decisions before strength tuning;
- hand-feel constraints alongside accuracy and speed;
- configuration provenance and canonical defaults;
- complexity budgets and gate deletion criteria;
- RED/GREEN regression evidence and real-runtime smoke validation.

The methodology ends with a proposed Codex skill package. It identifies which
references can be generic and which aim-specific examples belong only in this
repository. No global skill is installed in this task.

## Deliverable 3: Agent Context Sync

Apply `sync-20260720-001`:

- archive the current detailed `session-log.md` before rewriting it;
- rewrite `session-log.md` into a compact primary index with archive links,
  milestone summaries, and the new 2026-07-16 to 2026-07-20 entry;
- condense `handoff.md` to current objective, architecture, verified baseline,
  next action, active questions, relevant decisions, read-first files, and
  explicit anti-regression rules;
- link both new project documents from context;
- preserve user-confirmed statements separately from AI-inferred transferable
  conclusions;
- exclude credentials, personal paths, raw video paths, and unnecessary private
  data.

The existing session history is preserved in an archive file; compaction must
not silently discard or overwrite it.

## Evidence Sources

Use the following hierarchy:

1. User-confirmed outcomes and corrections from the active session.
2. Git commits from 2026-07-16 through 2026-07-20.
3. Accepted project specs, plans, and benchmark acceptance documents.
4. Fixed-seed benchmark artifacts and test output recorded in those documents.
5. AI inference, explicitly marked when evidence supports a generalization but
   does not prove it across other applications.

Do not treat historical artifacts lacking configuration provenance as direct
performance comparisons.

## Review Criteria

The completed record is acceptable when a future engineer can answer:

- Why did simplification initially weaken feel?
- Which old behavior was valuable and which was accidental stacking?
- Why did early benchmarks miss practical improvements?
- How did the team make user error, occlusion, slowdown, and state transitions
  measurable?
- Why is the final solution an authority/fusion architecture rather than a new
  collection of gates?
- Which methods transfer to robotics, camera control, assisted steering, or
  other real-time human-AI control systems?
- What evidence would be required before changing the current baseline again?
