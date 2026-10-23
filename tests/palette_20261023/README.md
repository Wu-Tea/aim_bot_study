# Palette diagnostics

Read `docs/project/MARKER_PALETTE_DEV_20261024.md` for assumptions and limitations.

`freeze.py <pre-palette-revision>` materializes the old color/selector implementation in a new `runs/palette-20261023/reference` directory. It refuses to overwrite an existing reference. The saved reference from this session is the pre-edit dev implementation; the later Git-based script supports reconstruction from a revision that still has the old yellow/red/green classifiers.

Build with CMake using `-S tests/palette_20261023 -B runs/palette-20261023/build -A x64`, then `--build ... --config Release`. It links the already built production Vision library; rebuild the harness after updating that library.

`run.py --yellow-only` replays the two videos and their existing raw person boxes. Recoloring occurs outside timing. This is not rerun neural inference or real new-palette footage. Omitting `--yellow-only` is an additional stress test that merges the old yellow and red enemy hue classes; it is not an equivalence control.

`probe.py` recolors only manually located real diamond markers in frames120/864 while preserving surrounding green foliage and per-pixel value/saturation. The probe verifies enemy pairing/continuation and friendly rejection. Original videos remain untouched. No physical input/output is opened by these tools.
