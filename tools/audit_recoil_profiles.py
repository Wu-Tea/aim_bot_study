from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

_REPO_ROOT = Path(__file__).resolve().parents[1]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from vision.recoil_collection.audit import audit_recoil_profile_directory


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Audit recoil profile readiness")
    parser.add_argument("--profile-dir", default="artifacts/recoil_profiles")
    args = parser.parse_args(argv)

    report = audit_recoil_profile_directory(Path(args.profile_dir))
    payload = {
        "root": str(report.root),
        "profiles": [
            {
                "profile_id": profile.profile_id,
                "aim_mode": profile.aim_mode,
                "confidence": profile.confidence,
                "sample_count": profile.sample_count,
                "duration_ms": profile.duration_ms,
                "findings": list(profile.findings),
                "runtime_ready": profile.runtime_ready,
            }
            for profile in report.profiles
        ],
    }
    print(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
