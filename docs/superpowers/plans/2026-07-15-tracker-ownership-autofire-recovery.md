# Tracker Ownership and Autofire Recovery Implementation Plan

1. Add failing Autofire tests for unique vision-frame readiness and persistent observed authority across controller ticks.
2. Add failing target snapshot tests for short empty-frame ownership hold, competing-track rejection, owner reacquisition, and eventual replacement after owner expiry.
3. Implement canonical ownership in `TargetSnapshotProvider` using the existing tracker estimate lifetime.
4. Separate current observed source validity from once-per-frame consumption.
5. Make `AutoFireGate` count readiness by `vision_sequence` and report a block reason.
6. Thread Autofire decision diagnostics through controller output components and runtime telemetry.
7. Add a live-rate integration benchmark covering 80 Hz vision, 1 kHz controller, scope occlusion, opposite-side candidates, and final RB output.
8. Run affected tests, all native tests, left-stick `--require-fixed`, full Release build, and rebuild the main runtime after merge.

