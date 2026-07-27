# Documentation Entry And Archive Design

**Date:** 2026-07-27  
**Status:** Approved and implemented
**Scope:** Repository documentation only; no production code or runtime configuration changes

## Purpose

Make the repository understandable from a small set of current entry documents
without discarding the evidence accumulated during earlier controller, Vision,
benchmark, performance, and research work.

The documentation currently mixes four different lifecycles:

1. current runtime and architecture reference;
2. current benchmark contracts;
3. historical baselines, acceptance records, plans, and worklogs;
4. reusable cross-project methods.

The reorganization will make those lifecycles explicit. Historical material
will remain tracked and searchable, but it will no longer compete with current
operational guidance in the primary reading path.

## User-Facing Reading Path

The primary reading path will be:

1. root `README.md` for a concise project introduction, startup, build,
   telemetry, and verification quick start;
2. `docs/README.md` for the complete documentation map;
3. `docs/project/CURRENT_STATE.md` for the current production architecture,
   verified behavior, open work, and non-regression boundaries;
4. domain references under `docs/project/`;
5. benchmark contracts under `docs/benchmarks/`;
6. historical evidence under `docs/archive/`.

The root README must not reproduce detailed architecture or historical
benchmark results. It links to the maintained sources instead.

## New And Rewritten Entry Documents

### Root `README.md`

Rewrite the root README as a concise operational entry containing:

- what the project is and who uses it;
- the default native C++ runtime and Python fallback boundary;
- the normal foreground and background launch paths;
- the current `480x416` capture and TensorRT engine contract;
- the minimum build and verification commands;
- the distinction between `[runtime.telemetry].enabled = true` and
  `--perf-log`;
- links to `docs/README.md`, `docs/project/CURRENT_STATE.md`, and the current
  native runtime reference.

Detailed launcher matrices, architecture walkthroughs, and benchmark history
will move out of the root README.

### `docs/README.md`

Create the central documentation map with these sections:

- Start here;
- Current system;
- Running, building, logging, and debugging;
- Benchmark contracts;
- Vision/model training and recoil operations;
- Reusable engineering methods;
- Historical archive;
- Design and implementation records.

Each listed document must have a one-sentence purpose and a lifecycle label
where ambiguity is possible.

### `docs/project/CURRENT_STATE.md`

Create one living current-state document with:

- last-reviewed date, branch, and evidence boundary;
- runtime identity and `480x416` Vision/engine contract;
- current selector, tracker, ADS, BodyLock, dynamics, fusion, AutoFire, recoil,
  and ViGEm ownership;
- current telemetry and session-log behavior;
- verified benchmark conclusions;
- active work, headed by dynamic ROI evidence and bounded translation;
- unresolved questions and live-smoke requirements;
- explicit non-regression boundaries, including no duplicate controller gates,
  no permanent BodyLock brake, no weapon database, and no additional production
  Vision inference by default.

This document summarizes current facts. It links to evidence rather than
copying full result tables.

### `docs/project/README.md`

Rewrite the project index so it lists current project references only.
Historical worklogs, dated acceptance reports, superseded benchmark summaries,
and paused research must be reached through the archive index instead.

### `docs/benchmarks/README.md`

Create an index that distinguishes:

- maintained benchmark contracts and runners;
- current authoritative baselines;
- archived result narratives.

It must state that comparisons require compatible revision, runtime/config
fingerprint, schema, seeds, duration, and scenario semantics.

### `docs/archive/README.md`

Create an archive index explaining that archived documents are evidence and
history, not current runtime truth. Organize links under:

- control history;
- project history;
- performance history;
- research;
- benchmark results.

## Archive Policy

No historical document will be deleted. Moves will use Git-preserving file
renames where possible.

### Keep Under `docs/project/`

Keep maintained references for:

- native runtime and Vision;
- project, controller, Vision, gamepad, and mouse architecture;
- native log sessions;
- current detector training, dataset, and recoil operations.

Documents that contain both current instructions and historical narrative stay
in place only when the current instructions are still authoritative. Their
entry descriptions must disclose their purpose.

### Keep In Existing Historical Stores

Keep `docs/superpowers/specs/` and `docs/superpowers/plans/` in place. They
already have an explicit design/implementation-history lifecycle and moving
them would create high link churn without improving current navigation.

Keep reusable project-neutral material under `docs/methods/`.

### Move To `docs/archive/control-history/`

Move dated controller baselines, acceptance reports, audits, tuning sweeps, and
optimization histories that describe completed stages rather than the current
operational contract.

### Move To `docs/archive/project-history/`

Move superseded project tracking, worklog, old goal, and root-cleanup records.

### Move To `docs/archive/performance-history/`

Move completed or superseded performance plans and acceptance reports whose
commands or targets are no longer the primary operational entry.

### Move To `docs/archive/research/`

Move paused audio direction, audio/visual fusion, fullscreen canvas, and related
research material that is not part of the current production path.

### Move To `docs/archive/benchmark-results/`

Move old result summaries that have been superseded by the maintained native
benchmark contracts or newer acceptance evidence.

The implementation plan must enumerate every moved path before execution. It
must not classify a document as historical solely because it is old; the
deciding question is whether it still defines current behavior or operation.

## Link And Compatibility Policy

- Update tracked Markdown links affected by moves.
- Replace obsolete absolute local paths with repository-relative links or plain
  repository paths.
- Do not rewrite unrelated code samples that merely resemble Markdown links.
- Do not modify dirty `.agent-context` files during this documentation slice.
  Existing broken links there will be reported separately because those files
  contain ongoing user work.
- Plain-text references in active entry documents must point to the new
  canonical locations.
- No redirect stubs are required for documents that are only referenced within
  this repository after all tracked references are updated.

## Current-State Content Boundary

The current-state document will record these established facts:

- the native C++ gamepad runtime is the production default;
- Python is fallback and tooling;
- capture and engine identity are `480x416`;
- ADS snap belongs to one physical ADS epoch;
- BodyLock follows a tracker-owned trajectory and may retain bounded target
  inertia;
- green friendly cues are rejected and yellow enemy cues are auxiliary;
- AutoFire uses the current pulse/hold and physical-fire passthrough contract;
- recoil remains final feed-forward;
- the controller uses one target lifecycle owner and one final fusion/shaping
  path;
- runtime telemetry is the structured local evidence path, while `--perf-log`
  is console performance diagnostics;
- short contiguous Vision occlusion reproduces undertracking and reveal debt,
  but has not reproduced controller-generated twitch;
- dynamic ROI is the next high-priority evidence-driven Vision/control
  integration candidate, beginning with bounded translation, coordinate
  conversion, ADS reset, and no-target recentering before dynamic scaling.

Statements without current repository evidence must be marked as inferred or
open rather than presented as verified.

## Verification

The documentation slice is complete only when:

1. the root README provides a usable five-minute entry;
2. every current reference is reachable from `docs/README.md`;
3. every moved historical document is reachable from
   `docs/archive/README.md`;
4. the project index does not treat historical worklogs or superseded plans as
   current truth;
5. tracked relative Markdown links affected by the moves resolve;
6. obsolete `vision.py` and machine-specific absolute links are absent from
   current entry documents;
7. production source and runtime configuration remain unchanged;
8. pre-existing untracked files and dirty `.agent-context` work remain
   untouched.

## Non-Goals

- Deleting or rewriting historical evidence;
- reorganizing source code or runtime artifacts;
- changing production behavior or configuration;
- rewriting all 121 historical superpowers specifications and plans;
- treating documentation age alone as proof that a document is obsolete;
- updating active `.agent-context` files that contain unrelated uncommitted
  work.
