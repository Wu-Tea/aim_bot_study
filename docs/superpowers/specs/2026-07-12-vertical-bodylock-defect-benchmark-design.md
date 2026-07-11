# Vertical Bodylock Defect Benchmark Design

**Date:** 2026-07-12
**Status:** Approved for benchmark-first investigation

## Goal

Determine with deterministic evidence whether the current vertical target-point
geometry and bodylock behavior reproduce two reported defects, before changing
any controller or vision algorithm.

## Scope

This phase adds benchmark scenarios and measurements only. It must not change
target selection, bodylock, manual mixing, tracking, or runtime configuration.

## Ground Truth Model

Each scenario keeps four Y-axis concepts separate:

- detection box: the rectangle received by the current selector/controller;
- visible body interval: oracle ground truth for pixels occupied by the enemy;
- enemy cue Y: the marker above the visible body;
- reticle Y: the simulated user/AI output position.

The benchmark fails when the computed target or sustained reticle position lies
outside the visible body interval even if it remains inside the detection box.

## Scenario 1: Cooperative Upward Overshoot and Occlusion

The reticle starts below a visible target. For an initial phase, both user input
and AI correction move upward. After the reticle crosses the body target, the
benchmark introduces a short detection dropout representing weapon occlusion.
It records:

- maximum vertical overshoot beyond the visible body;
- frames until downward correction begins;
- AI output direction and magnitude during dropout;
- consecutive frames outside the visible body;
- reacquisition time after observations return;
- frames where AI output deepens the overshoot or opposes recovery input.

The defect is reproduced if the reticle leaves the body, remains outside through
the dropout, and does not begin a bounded recovery when user input reverses.

## Scenario 2: Prone / Low-Stair Air Lock

The detection box includes an upper cue/gap region, while the oracle visible
body occupies its lower portion. The selector and controller receive the normal
detection/body box. The user then applies sustained input toward the visible
body. The benchmark records:

- computed selector target Y and bodylock target Y;
- distance from each target point to the visible body interval;
- frames where AI opposes user movement toward the body;
- manual input required to leave the air-lock region;
- time until the reticle first enters and remains inside the visible body;
- final vertical error and bodylock mode duration.

Two variants use a stationary prone body and a downward-moving low-stair body.
The defect is reproduced if the target point remains above the body or bodylock
prevents bounded user correction into the body.

## Output

The native gamepad benchmark JSON and console summary gain a dedicated defect
section with per-scenario metrics and compact frame-event diagnostics. Existing
scenario results remain unchanged.

## Verification

1. Add self-tests proving the scenario generator has distinct detection and
   visible-body geometry and that its manual input points toward the body.
2. Run the new scenarios on the current code and retain the failing metrics.
3. Run all existing benchmark self-tests to ensure the harness itself does not
   alter established scenarios.
4. Only after the defect is reproduced, design and test an algorithm fix against
   the retained benchmark artifacts.
