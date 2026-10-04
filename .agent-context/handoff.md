# Agent Handoff

Last updated: 2026-10-04
Active scope: completed isolated C++ runtime refactor; use the current project model for continuation.

## Current State

- User authorized one-pass work in a new worktree without repeated coordination. `codex/cognition-refactor-20261004` inherits current source/doc changes at `1251273`; original workspace is untouched. The subsequent scoped refactor commit is the review unit.
- Default product path is C++. Python GUI/config/process control and native bridges remain dependencies; explicit Python gameplay fallback is retained. Default C++ does not authorize removal of all Python.
- SHA256 validation/provenance and benchmark acceptance/comparison/release tooling are retired. Keep ordinary functional tests and numerical simulations; do not restore historical manifests or gate requirements from older context.
- Long-term model and source links: [Current State](../docs/project/CURRENT_STATE.md). It covers capabilities, owners, initialization, operation, configuration commit and shutdown; implementation remains the source of truth.
- Shared Engine Adapter, reload policy and Bridge RAII from the inherited first batch are retained. Current work separates Loop diagnostics, moves IPC implementation out of its header, consolidates telemetry payloads and Fusion conversion, and removes redundant derived state. No controller algorithm change.
- Confirmed boundary defects fixed with failing-before/passing-after regressions: explicit malformed configuration values silently defaulted/parsed by prefix; disabled output still constructed a ViGEm device. Strict scalar parsing and conditional owned output construction correct these at their owners.

## Verification and Limits

- Fresh Release all-target build and CTest 50/50 groups pass, including retained numerical simulations. Affected Python GUI/config/startup/native bridge tests: 134/134 pass; full 978 collection was not run.
- Real C++/TensorRT/desktop-capture process, output disabled: control-value and vision-policy hot reload both reach applied revision 1 through Python IPC and production Loop; named stop event exits with code 0. Temporary telemetry contains current controller/delivery/acquisition/capture records. Real Fusion publisher-to-shared-channel geometry/version delivery and stop also pass; Canvas was not opened. No ViGEm construction or physical output occurred.
- Real receiver neutral output, physical device reconnect, Fusion window capture exclusion and gameplay feel were not retested. Offline and output-disabled integration results do not prove gameplay acceptance or speed improvement. Independent coordinating-chat review of this worktree has not happened.
- Original user feel confirmation from September applied only to ordinary multiplayer at that historical checkpoint. Old audit/hash/gate records cannot re-establish current acceptance requirements.

## Continuation Boundaries

- Keep raw zero-deadzone manual passthrough, continuous 15–30% authority, target acquisition/identity/handover, same-tick controller chain and resource/button release.
- VisionService request/policy fences and DeliveryGate source identity/age protect different invariants. Keep boundary checks unless their ownership and redundancy are proved.
- Diagnostics consume actual receipts; output-disabled records are not proof of delivery. Add scalar fields to the existing payload and producer/serializer/reader, not another parallel DTO.
- No automatic merge into the original workspace, no Python/UI/training feature retirement, no algorithm optimization and no extra governance/service framework are part of this change.

## Context Review

SyncSet: replace this outdated active handoff and append one milestone to session-log, within this isolated worktree only. Reviewer disposition: accept_draft (self-review): authorization is the user's one-pass completion request; current facts, historic decisions and unverified device behavior are distinguished; no secrets, raw captures, logs or project data are written into project-cognition. No existing decision is rewritten.
