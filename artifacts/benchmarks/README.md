# Benchmark Artifacts

This subfolder is intentionally source-controlled. It stores small historical benchmark snapshots used by docs and regression analysis.

Most other `artifacts/` subfolders are local runtime output and are ignored, including:

- `artifacts/recoil_app/`
- `artifacts/recoil_profiles/`
- `artifacts/recoil_plots/`
- `artifacts/mouse_telemetry/`
- `artifacts/video_debug/`
- `artifacts/weapon_examples/`

Keep new generated captures out of git unless they are deliberately promoted into a small, named benchmark fixture.
