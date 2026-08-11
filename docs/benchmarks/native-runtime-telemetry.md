# Native runtime telemetry and causal journal

The runtime telemetry pipeline is the debug-only evidence stream used to replay
controller delivery, committed target observations, ADS transitions, and response
windows. It is consumed by benchmark and causal-response replay tools; the live
controller does not read the JSONL output.

## Enablement

Set `[runtime.telemetry].enabled = true` in the active native configuration. The
legacy `aim_perf_file_log` switch remains a compatibility alias. When both are
false, no telemetry queue, writer thread, session directory, or causal journal
file is created.

All records use the existing bounded asynchronous queue. G0 adds two record
types under schema `causal_response_journal_v1`:

- `delivered_control_sample` v2: compact final right/left output and delivery
  status at the timestamp after the virtual-gamepad update returned. Controller
  decomposition remains in `controller_sample`; runtime identity remains in the
  per-file `session_metadata` record;
- `committed_capture_observation`: the capture-time geometry for the exact
  observation selected by the production coordinator, never a newer raw
  challenger or tracker-projected future point.

`vision_sample_quality` describes evidence quality. The separate
`identification_update_outcome` describes whether a learner update occurred and
why it was accepted or rejected. A visually normal frame is not counted as a
successful learning update.

## Fresh log and retention

Each run writes beneath one `runs/native_perf/sessions/<session-id>/` directory.
`runs/native_perf/fresh_session.json` points to the current or most recent
session. Rotation creates additional JSONL parts inside that same directory and
each part begins with session metadata containing revision, configuration,
engine, crop, and runtime provenance.

Cleanup must operate on whole closed session directories, identified by the
`.closed` marker. Never delete individual JSONL parts from a retained session,
because that makes sequence completeness and replay conclusions invalid. Do not
remove a directory containing `.active`. For quick manual cleanup, keep the
session referenced by `fresh_session.json` plus any benchmark-referenced
sessions, then remove older closed session directories as units.
