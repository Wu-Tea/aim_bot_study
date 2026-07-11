# Vertical Bodylock Defect Fix Design

## Goal

Eliminate air locking for wide/low targets and prevent bodylock from fighting deliberate vertical user correction, without regressing established standing and moving-target benchmarks.

## Design

1. The vision selector remains the owner of the selected aim point. Wide/low detections use a lower-body-safe vertical ratio instead of the generic midpoint when their aspect ratio indicates prone/low posture.
2. Bodylock consumes `target_y` from the selected target as its vertical lock reference. It may add bounded motion lead, but it must not replace that reference with a second fixed body-box ratio. Horizontal body center behavior remains unchanged.
3. When manual Y input is materially opposite the planned bodylock Y output, bodylock yields the Y axis for that frame. X assistance remains independent. This prevents sustained resistance during correction and recovery.
4. No new runtime configuration is added. Recoil, ADS snap, tracker backend, detector inference, and auto-fire are out of scope.

## Acceptance

- Prone and low-stair synthetic targets land inside the visible-body oracle.
- Manual escape reaches the body and sustained AI-opposition frames are eliminated or reduced to transient frames.
- Cooperative upward pull plus occlusion does not add AI resistance when the user reverses; recovery begins on the first reverse-input frame. Total overshoot from forced manual input remains diagnostic rather than being hidden by AI counter-input.
- Existing selector, controller, gamepad self-test, benchmark metrics, and pipeline contract pass.
- Full native gamepad benchmark comparison shows no material regression in established standing, moving, slide, and jump bodylock scenarios.
