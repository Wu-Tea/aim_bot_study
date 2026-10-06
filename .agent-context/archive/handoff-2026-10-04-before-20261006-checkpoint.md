# Agent Handoff

Last updated: 2026-10-04
Active scope: native refactor merged into dev; dev GUI preserved, new C++ GUI excluded.

## Current state

- Latest user explicitly requested merge into dev and preserve dev GUI. This supersedes the prior blanket Python retirement for desktop UI. Native gameplay/control/Vision/build stay C++; Python is retained only for desktop configuration/curves/process management/IPC and GUI checks. Do not restore Python gameplay, training or analysis paths.
- dev GUI baseline 0ab4020, pre-merge cleanup/source preservation 05d1c83. Original untracked output screenshots/reports and ignored models/config/profiles remain intact. Refactor branch remains available independently; it contains the excluded C++ GUI.
- GUI implementation, root VBS, GUI docs and background start/stop/Fusion launch scripts exactly preserve 05d1c83. New native desktop sources/target/tests are excluded from dev. Native IPC protocol is shared and unchanged (protocol=1, snapshot=640 bytes).
- Merged runtime ownership/RAII/diagnostics/telemetry refactor, strict config parsing, optional output resource creation, historical test cleanup, pybind and non-GUI Python retirement, native SDL/ViGEm DLL inputs. Preserve dev independent game-profile parsing and its regression; no production controller algorithm change.
- Current model/links: [Current State](../docs/project/CURRENT_STATE.md); build/entry: [README](../README.md); GUI: [Desktop Assistant](../docs/project/DESKTOP_ASSISTANT.md).

## Verification and limits

- Merged dev full Release build passes. CTest 50/50 groups and retained GUI unittest 66/66 pass. GUI/native config assertions include independent-game identity and malformed inputs.
- Actual retained Python RuntimeManager/ControlChannel starts merged TensorRT runtime with device output disabled, control reload applies revision 0→1, graceful stop completes exit=0 and owned active record becomes inert. Temporary config isolated; original config/profile assets unchanged.
- GUI source/entry/scripts have no diff versus pre-merge dev. No new live receiver, gameplay, Canvas capture exclusion or feel acceptance; disabled output does not prove delivery. Engine export untested without ONNX.
- Earlier isolated branch ignored-cache recursive cleanup was rejected by automatic review with blocked by policy; not retried/bypassed. Historical ignored outputs may remain, outside current build paths.

## Continuing boundaries

- SHA256 checking/provenance and benchmark comparison/release framework stay retired. GUI path-derived mutex names do not constitute provenance validation. Ordinary tests and useful numerical simulations remain.
- Preserve zero-deadzone raw manual passthrough, continuous 15–30% intent, search/acquisition/identity/handover, same-tick controller chain and resource/button release. ai_proposal knobs remain unknown/inert.
- GUI uses the existing Python 3.11+ Tk/stdlib environment; no retired vision/controller Python packages are needed. Native build needs SDKs/DLLs and compatible models, not Python.

## Context review

SyncSet: update current dev handoff/model and append merge milestone. Reviewer accept_draft (self-review) under user's direct merge/one-pass authorization. Latest scope overrides historical isolation/GUI-retirement claims; historic verification distinguished from merged results. No decision rewrite, secrets/raw logs/assets or writes to project-cognition.
