import json
import os
import tempfile
import time
from pathlib import Path

import pytest

from tools.manage_native_logs import CleanupPolicy, execute_prune, plan_prune


def make_session(root: Path, name: str, *, state="closed", age_days=10, pinned=False, size=32):
    session = root / "sessions" / name
    session.mkdir(parents=True)
    manifest = {
        "schema_version": 1,
        "session_id": name,
        "state": state,
        "updated_epoch": time.time() - age_days * 86400,
        "pinned": pinned,
    }
    (session / "session.json").write_text(json.dumps(manifest), encoding="utf-8")
    (session / (".active" if state == "active" else ".closed")).touch()
    (session / "telemetry.jsonl").write_bytes(b"x" * size)
    timestamp = time.time() - age_days * 86400
    os.utime(session / "session.json", (timestamp, timestamp))
    return session


def write_fresh(root: Path, session_id: str):
    (root / "fresh_session.json").write_text(
        json.dumps({"session_id": session_id, "relative_path": f"sessions/{session_id}"}),
        encoding="utf-8",
    )


def test_plan_protects_fresh_active_pinned_and_latest():
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        make_session(root, "old", age_days=30)
        make_session(root, "pinned", age_days=20, pinned=True)
        make_session(root, "active", state="active", age_days=20)
        make_session(root, "recent_a", age_days=3)
        make_session(root, "recent_b", age_days=2)
        make_session(root, "fresh", age_days=10)
        write_fresh(root, "fresh")

        plan = plan_prune(root, CleanupPolicy(min_age_days=1, keep_latest=2))
        assert [entry.session_id for entry in plan.delete] == ["old"]
        protected = {entry.session_id: entry.reason for entry in plan.protected}
        assert protected["fresh"] == "fresh"
        assert protected["active"] == "active"
        assert protected["pinned"] == "pinned"
        assert protected["recent_a"] == "latest"
        assert protected["recent_b"] == "latest"


def test_dry_run_does_not_delete_and_execute_removes_whole_session():
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        doomed = make_session(root, "doomed", age_days=30, size=128)
        plan = plan_prune(root, CleanupPolicy(min_age_days=1, keep_latest=0))
        result = execute_prune(plan, dry_run=True)
        assert doomed.exists()
        assert result.deleted_sessions == 0
        result = execute_prune(plan, dry_run=False)
        assert not doomed.exists()
        assert result.deleted_sessions == 1
        assert result.reclaimed_bytes >= 128


def test_corrupt_or_outside_entries_are_never_deleted():
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        corrupt = root / "sessions" / "corrupt"
        corrupt.mkdir(parents=True)
        (corrupt / "session.json").write_text("not json", encoding="utf-8")
        (corrupt / ".closed").touch()
        plan = plan_prune(root, CleanupPolicy(min_age_days=0, keep_latest=0))
        assert not plan.delete
        assert plan.protected[0].reason == "invalid_manifest"
        execute_prune(plan, dry_run=False)
        assert corrupt.exists()
