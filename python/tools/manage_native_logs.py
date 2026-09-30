#!/usr/bin/env python3
"""List and safely prune whole native debug-log sessions."""

from __future__ import annotations

import argparse
import json
import shutil
import time
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class CleanupPolicy:
    min_age_days: float = 1.0
    keep_latest: int = 2
    max_closed_sessions: int | None = None
    max_total_bytes: int | None = None


@dataclass(frozen=True)
class SessionEntry:
    session_id: str
    path: Path
    state: str
    updated_epoch: float
    size_bytes: int
    reason: str = "eligible"


@dataclass(frozen=True)
class PrunePlan:
    root: Path
    delete: tuple[SessionEntry, ...] = ()
    protected: tuple[SessionEntry, ...] = ()


@dataclass(frozen=True)
class PruneResult:
    deleted_sessions: int = 0
    reclaimed_bytes: int = 0
    dry_run: bool = True


def _directory_size(path: Path) -> int:
    total = 0
    for item in path.rglob("*"):
        if item.is_file() and not item.is_symlink():
            try:
                total += item.stat().st_size
            except OSError:
                pass
    return total


def _fresh_session_id(root: Path) -> str:
    try:
        data = json.loads((root / "fresh_session.json").read_text(encoding="utf-8"))
        return str(data.get("session_id", ""))
    except (OSError, ValueError, TypeError):
        return ""


def _load_session(path: Path) -> SessionEntry:
    try:
        if path.is_symlink():
            raise ValueError("session directory is a link")
        data = json.loads((path / "session.json").read_text(encoding="utf-8"))
        session_id = str(data["session_id"])
        if session_id != path.name:
            raise ValueError("session id/path mismatch")
        state = "active" if (path / ".active").exists() else str(data.get("state", "unknown"))
        if state == "closed" and not (path / ".closed").exists():
            state = "unknown"
        updated = float(data.get("updated_epoch", (path / "session.json").stat().st_mtime))
        return SessionEntry(session_id, path, state, updated, _directory_size(path))
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError):
        return SessionEntry(path.name, path, "invalid", 0.0, 0, "invalid_manifest")


def discover_sessions(root: Path) -> list[SessionEntry]:
    sessions_root = root.resolve() / "sessions"
    if not sessions_root.exists():
        return []
    entries: list[SessionEntry] = []
    for path in sessions_root.iterdir():
        if path.is_dir():
            entries.append(_load_session(path))
    return entries


def plan_prune(root: Path, policy: CleanupPolicy) -> PrunePlan:
    root = root.resolve()
    fresh_id = _fresh_session_id(root)
    protected: list[SessionEntry] = []
    candidates: list[SessionEntry] = []
    for entry in discover_sessions(root):
        if entry.reason == "invalid_manifest":
            protected.append(entry)
        elif entry.session_id == fresh_id:
            protected.append(replace(entry, reason="fresh"))
        elif entry.state == "active":
            protected.append(replace(entry, reason="active"))
        else:
            try:
                manifest = json.loads((entry.path / "session.json").read_text(encoding="utf-8"))
            except (OSError, ValueError):
                protected.append(replace(entry, reason="invalid_manifest"))
                continue
            if bool(manifest.get("pinned", False)):
                protected.append(replace(entry, reason="pinned"))
            elif entry.state != "closed":
                protected.append(replace(entry, reason="not_closed"))
            else:
                candidates.append(entry)

    candidates.sort(key=lambda item: item.updated_epoch, reverse=True)
    keep_count = max(0, policy.keep_latest)
    for entry in candidates[:keep_count]:
        protected.append(replace(entry, reason="latest"))
    remaining = candidates[keep_count:]
    now = time.time()
    old_enough: list[SessionEntry] = []
    for entry in remaining:
        age_days = (now - entry.updated_epoch) / 86400.0
        if age_days < max(0.0, policy.min_age_days):
            protected.append(replace(entry, reason="too_new"))
        else:
            old_enough.append(entry)

    delete_ids: set[str] = set()
    if policy.max_closed_sessions is None and policy.max_total_bytes is None:
        delete_ids.update(entry.session_id for entry in old_enough)
    if policy.max_closed_sessions is not None:
        allowed = max(0, policy.max_closed_sessions - keep_count)
        oldest_first = sorted(old_enough, key=lambda item: item.updated_epoch)
        excess = max(0, len(remaining) - allowed)
        delete_ids.update(entry.session_id for entry in oldest_first[:excess])
    if policy.max_total_bytes is not None:
        total = sum(entry.size_bytes for entry in protected + remaining)
        for entry in sorted(old_enough, key=lambda item: item.updated_epoch):
            if total <= max(0, policy.max_total_bytes):
                break
            delete_ids.add(entry.session_id)
            total -= entry.size_bytes

    delete = [replace(entry, reason="policy") for entry in old_enough if entry.session_id in delete_ids]
    for entry in old_enough:
        if entry.session_id not in delete_ids:
            protected.append(replace(entry, reason="within_policy"))
    delete.sort(key=lambda item: item.updated_epoch)
    protected.sort(key=lambda item: item.session_id)
    return PrunePlan(root, tuple(delete), tuple(protected))


def execute_prune(plan: PrunePlan, *, dry_run: bool) -> PruneResult:
    sessions_root = (plan.root / "sessions").resolve()
    deleted = 0
    reclaimed = 0
    if dry_run:
        return PruneResult(0, 0, True)
    for entry in plan.delete:
        resolved = entry.path.resolve()
        if resolved.parent != sessions_root or entry.path.is_symlink():
            continue
        if not (resolved / ".closed").exists() or (resolved / ".active").exists():
            continue
        shutil.rmtree(resolved)
        deleted += 1
        reclaimed += entry.size_bytes
    return PruneResult(deleted, reclaimed, False)


def _print_entries(entries: Iterable[SessionEntry]) -> None:
    for entry in entries:
        print(f"{entry.session_id}\t{entry.state}\t{entry.size_bytes}\t{entry.reason}\t{entry.path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    list_parser = subparsers.add_parser("list")
    list_parser.add_argument("--root", type=Path, default=Path("runs/native_perf"))
    prune_parser = subparsers.add_parser("prune")
    prune_parser.add_argument("--root", type=Path, default=Path("runs/native_perf"))
    prune_parser.add_argument("--dry-run", action="store_true")
    prune_parser.add_argument("--max-age-days", type=float, default=7.0)
    prune_parser.add_argument("--keep-latest", type=int, default=2)
    prune_parser.add_argument("--max-closed-sessions", type=int)
    prune_parser.add_argument("--max-total-gb", type=float)
    args = parser.parse_args()
    if args.command == "list":
        _print_entries(discover_sessions(args.root.resolve()))
        return 0
    max_bytes = None if args.max_total_gb is None else int(args.max_total_gb * 1024**3)
    plan = plan_prune(
        args.root,
        CleanupPolicy(args.max_age_days, args.keep_latest, args.max_closed_sessions, max_bytes),
    )
    _print_entries(plan.delete)
    result = execute_prune(plan, dry_run=args.dry_run)
    print(
        f"deleted_sessions={result.deleted_sessions} "
        f"reclaimed_bytes={result.reclaimed_bytes} dry_run={str(result.dry_run).lower()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
