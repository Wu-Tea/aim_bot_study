# Mouse ADS Commit-Hold Design

**Goal:** Replace the current native mouse controller behavior with a new state-machine-based assist model optimized for low-to-mid TTK mouse fights, using ADS entry help, commitment-gated hold, short reacquire bridging, and immediate user-intent release.

**Scope:**
- Replace the current `controllers/mouse/` behavior model instead of incrementally tuning it.
- Design the new mouse assist around four runtime states:
  - `manual`
  - `ads_entry_assist`
  - `commit_hold`
  - `reacquire_bridge`
- Extend the controller-side target metadata boundary enough for the mouse controller to reason about target source and target continuity.
- Keep the vision/controller architectural split small:
  - vision selects and shapes targets
  - mouse controller reads manual intent and owns final mouse actuation
- Support both Python vision and native C++ vision backends with one controller-facing contract.

**Non-goals:**
- No preservation of the old `controllers/mouse/ai_aim.py` semantics as a compatibility mode.
- No attempt to port the gamepad `ads_snap` and `body_lock` logic directly into mouse code.
- No redesign of target selection heuristics inside vision for this spec.
- No attempt to make the first mouse version depend on `slow_zone` or `fire_zone` crossing the controller boundary.
- No commitment in this spec to exact numeric tuning beyond initial default bands and gating intent.

## Problem

The current native mouse path is intentionally thin:

- additive correction on top of the live mouse
- a single AI correction plugin
- pulse auto-fire
- simple recoil pull-down

That design is easy to reason about, but it is too flat for the desired combat behavior. It does not distinguish between:

- the first ADS alignment moment
- a player who has already committed to a target
- a short continuity break on the same target
- a deliberate user flick away from the current target

For mouse, these phases should not share one generic correction law. Mouse fights are won or lost not only by raw correction amount, but by whether the assist becomes active at the right moment and disappears at the right moment.

The design target here is a game like Delta Force with practical TTK around `350 ms`. In that environment:

- the first `80-150 ms` after ADS matters a lot
- a small amount of post-entry stabilization is useful
- overly sticky or persistent assist becomes harmful quickly
- player intent release is more important than raw assist uptime

## Why The Old Mouse Path Should Be Replaced

The old mouse path encodes one generic idea:

> while aiming, add some correction and damp some manual movement

That is not the desired behavior anymore.

The new design changes the model, not just the tuning:

- the assist is phase-aware
- target commitment matters
- short same-target recovery is different from first acquisition
- release behavior has explicit priority over continued assistance

Because this is a behavioral rewrite rather than a parameter retune, the old mouse logic should be treated as legacy and removed from the primary path.

## Native Vision Feasibility

The native C++ vision module already produces enough core information to support this design, but the current controller handoff does not preserve all of it.

### What native vision already provides

`native/vision_native/include/vision_native/types.h` defines a `VisionResult` carrying:

- `dx`
- `dy`
- `target_x`
- `target_y`
- `screen_center_x`
- `screen_center_y`
- `has_body_box`
- `body_x1/y1/x2/y2`
- `target_source`

The native target selector also already maintains several short-horizon continuity behaviors:

- first pickup confirmation: `2` frames
- target hold after loss: `2` frames
- target switch confirmation: `2` frames
- short prediction budget: `2` predicted frames

It emits target source semantics:

- `observed`
- `reconstructed`
- `predicted`

The native aim enhancement stage already reasons differently about predicted targets and near-target damping, which is a good fit for the new mouse gating model.

### What the current controller boundary loses

The existing `ControllerTarget` dataclass only carries:

- `aim_point_x`
- `aim_point_y`
- `screen_center_x`
- `screen_center_y`
- optional `body_box`

The current native runner converts native results into `ControllerTarget`, but drops `target_source`.

That means native vision can already compute a large portion of the semantics the new mouse controller needs, but the current controller boundary hides one critical signal:

- whether the current target is `observed`, `reconstructed`, or `predicted`

### Feasibility conclusion

The design is feasible with native vision.

However, the spec requires one explicit boundary change:

> the controller-facing target metadata must preserve `target_source`

This is not optional if the new mouse controller is expected to distinguish:

- fresh stable tracking
- reconstructed continuity
- predicted short-horizon recovery

Without that source signal, the controller can still estimate continuity locally from body-box motion, but it cannot reliably know when vision itself considers the target predicted versus observed.

## Approaches Considered

### 1. Keep the old mouse path and retune it

Pros:
- lowest implementation cost
- least code churn

Cons:
- does not express phase-specific behavior
- keeps acquisition, hold, and release blended together
- keeps mouse assistance too generic

### 2. ADS-first model with fixed 30 px stabilization

Pros:
- simple mental model
- aligned with the need for fast ADS pickup

Cons:
- `30 px` is too static to be the whole near-target story
- risks turning stabilization into a broad sticky zone
- does not distinguish first entry from same-target continuity recovery

### 3. Commitment-gated state machine with short ADS entry help

Pros:
- best fit for mouse intent preservation
- separates first entry, hold, and short reacquire
- cleanly encodes release priority
- can consume native target source semantics directly

Cons:
- more stateful than the current mouse path
- requires expanding the controller-side metadata contract

## Chosen Design

Choose **Approach 3**.

The new mouse controller behavior will be driven by four states:

1. `manual`
2. `ads_entry_assist`
3. `commit_hold`
4. `reacquire_bridge`

With one global override rule:

5. `switch_release`
   - not a long-lived state
   - a highest-priority release condition that immediately returns the controller to `manual`

The governing principle is:

> mouse assist should protect good aim and compress the cost of entry, but it should not keep fighting the player once the player wants to leave.

## Runtime States

### `manual`

Default state.

Purpose:
- preserve natural mouse ownership
- avoid autonomous pull when target commitment is weak

Behavior:
- no meaningful autonomous snap
- no sticky near-target hold
- optional extremely weak correction is allowed, but the first version should assume none

Entry conditions:
- not aiming
- no valid target
- explicit release due to switch intent
- bridge timeout
- continuity collapse

Exit conditions:
- enter `ads_entry_assist` when ADS begins and the target passes entry gating
- enter `commit_hold` directly when the player has already manually brought the reticle into a commit-valid near-target region

### `ads_entry_assist`

Short front-loaded assist at the beginning of an ADS engagement.

Purpose:
- reduce the cost of the first precise alignment
- help the player reach a lethal line faster
- avoid turning first entry into broad controller-like snap behavior

Behavior:
- short, front-loaded correction burst
- low persistence
- no repeated re-triggering inside the same stable engagement

Entry conditions:
- newly entered ADS
- vision has a confirmed target
- target is not low-quality prediction
- error is inside an outer arming band
- no strong user breakaway flick

Exit conditions:
- enter `commit_hold` once error is small and continuity is stable
- return to `manual` if the entry window expires, the target is lost, or the user breaks away

### `commit_hold`

Primary state of the new mouse design.

Purpose:
- protect a target the player has already substantially committed to
- stabilize the first bullet string
- reduce recoil-driven or jitter-driven loss of line

Behavior:
- preserve helpful same-direction manual input
- lightly suppress clearly harmful opposing input
- lightly suppress orthogonal wobble near the committed target line
- allow light recoil settle support
- prefer stability over autonomous pull

This state is not a body lock in the gamepad sense. It is a commitment-aware stabilization mode.

Entry conditions:
- player is aiming
- target continuity is stable enough
- error is inside a smaller effective hold band
- the current target is still the same target in practical terms
- the player is not executing a clear exit flick

Exit conditions:
- enter `reacquire_bridge` on short same-target loss
- return to `manual` on target switch, strong release intent, ADS release, or sustained error growth

### `reacquire_bridge`

Short same-target recovery state.

Purpose:
- preserve fight continuity through brief target loss
- avoid treating every tiny interruption as a brand-new ADS acquisition

Behavior:
- tiny, short-lived bridge correction
- lower authority than `ads_entry_assist`
- only valid for the same recently committed target

Entry conditions:
- must come from `commit_hold`
- recent commitment was strong
- target was briefly lost or demoted in quality
- user has not signaled a switch or escape

Exit conditions:
- return to `commit_hold` when the same target is stably reacquired
- return to `manual` when the bridge expires or the player leaves

## Release Priority

`switch_release` is the highest-priority behavior rule in the system.

When triggered, it cancels all assist states and returns to `manual`.

This rule exists to preserve one mouse-specific guarantee:

> losing assist is preferable to dragging user intent.

Release should fire on signals such as:

- strong manual flick away from the current target line
- target switch confirmation from vision
- sudden large error expansion combined with user movement away
- end of ADS

## Required Controller-Facing Data Contract

The new mouse controller needs more than raw `dx/dy`.

### Required target fields

The controller-facing target payload must carry:

- `aim_point_x`
- `aim_point_y`
- `screen_center_x`
- `screen_center_y`
- `body_box`
- `target_source`

Recommended `target_source` values:

- `observed`
- `reconstructed`
- `predicted`

### Optional but not required for V1

The design does not require controller-side `slow_zone` or `fire_zone` in V1.

Instead:
- the controller may derive its own commit and hold regions from `body_box`
- vision remains responsible for autofire recommendation using its own `fire_zone`

If later testing shows controller-side hold logic benefits from directly knowing `fire_zone`, that can be added as a follow-up contract change.

## Signal Model

The mouse controller should reason about three kinds of signals:

### 1. Vision-provided target quality signals

- target exists / does not exist
- target source
- body box geometry
- current aim point and error

### 2. Local continuity signals

Derived inside the mouse controller:

- same-target continuity by body-box similarity and target-point proximity
- commit duration
- time since stable commitment
- time since target loss

### 3. Manual intent signals

Derived from live mouse input:

- manual speed magnitude
- manual direction relative to current error
- strong breakaway flick
- low-speed settling movement

The design must not rely on any single signal alone.

For example:
- a near target is not enough without commitment
- a stable target is not enough if the player is clearly flicking away

## Default Region Semantics

The user-proposed `30 px` value is accepted only as an **outer arming band**, not as the full-strength stabilization zone.

### Outer arming band

Default concept:
- around `30 px`

Role:
- allow `ads_entry_assist`
- allow transition toward `commit_hold`
- not a strong sticky zone by itself

### Effective hold band

Default concept:
- materially smaller than the outer band
- roughly `8-12 px` initially, or eventually body-box-relative

Role:
- enable `commit_hold`
- allow meaningful micro-stabilization

### Inner release-softening band

Very close to center, the controller should reduce authority again to avoid visible cursor fight and over-correction.

This prevents:
- buzzing near center
- apparent magnetic drag at point-blank precision

## State Transition Rules

### `manual -> ads_entry_assist`

Allowed when:
- ADS has just started
- target is confirmed
- target is not low-confidence prediction-only continuity
- error is within the outer arming band
- manual movement does not indicate a large intentional switch

### `manual -> commit_hold`

Allowed when:
- the player has already manually placed near target
- continuity is stable enough
- error is already inside the hold band

This allows the design to help a player who does not need entry help but does benefit from stabilization.

### `ads_entry_assist -> commit_hold`

Allowed when:
- error shrinks into the hold band
- target continuity remains stable
- the user is still engaging the same target

### `commit_hold -> reacquire_bridge`

Allowed when:
- recent commitment was valid
- the same target is briefly lost
- source or continuity quality temporarily degrades
- breakaway intent is absent

### `reacquire_bridge -> commit_hold`

Allowed when:
- same-target continuity returns within a short budget

### Any state -> `manual`

Required when:
- ADS ends
- switch release fires
- bridge expires
- target continuity no longer supports the current engagement

## Interaction With Native Vision Source Semantics

The new mouse controller should explicitly treat native target source differently:

- `observed`
  - strongest basis for commitment
  - valid for entry and hold
- `reconstructed`
  - valid for continuity preservation
  - acceptable for hold if recently committed
- `predicted`
  - valid only for short `reacquire_bridge`
  - should not start a strong fresh `ads_entry_assist`

This is the most important reason the new design requires `target_source` in the controller-facing payload.

## Actuation Philosophy

The mouse controller should not behave like a virtual stick.

Controller-side output should follow these rules:

- entry help is front-loaded, not sticky
- hold behavior favors damping and stabilization over dragging
- bridge behavior is weaker than entry help
- release is immediate and cheap

Put differently:

- `ads_entry_assist` compresses entry time
- `commit_hold` protects already-correct play
- `reacquire_bridge` preserves continuity
- `manual` preserves ownership

## Error Handling And Failure Modes

The design must explicitly avoid:

- wrong-target pull during first ADS entry
- sticky interference during target switch
- persistent help after target loss
- broad near-target magnetic feel
- oscillation around zero crossing
- treating predicted targets as equivalent to observed stable targets

The safest failure direction is:

> if confidence is unclear, release rather than persist

That rule should bias both implementation and tuning.

## Testing Strategy

### Unit-level state tests

Add state-machine tests covering:

- entry gating
- direct manual-to-hold transition
- hold activation and release
- short bridge timeout
- predicted-source restrictions
- switch release priority

### Controller contract tests

Add tests validating the expanded target metadata contract:

- `target_source` survives Python vision handoff
- `target_source` survives native vision handoff
- `ControllerTarget` includes body box and source together

### Synthetic combat-sequence tests

Add deterministic sequence tests for:

- ADS start onto one stable target
- stable hold with recoil-like disturbance
- brief same-target disappearance and bridge recovery
- two-target overlap and explicit target switch
- fast user flick away while assistance is active

### Native-vision-specific tests

Add tests verifying:

- `observed` can enter `ads_entry_assist`
- `predicted` cannot begin strong entry help
- `reconstructed` can preserve continuity after prior commitment

## Migration Plan

Migration should be explicit rather than incremental.

### Phase 1

Expand the target metadata contract:

- add `target_source` to controller-facing target metadata
- preserve it through both Python and native vision runners

### Phase 2

Introduce the new mouse state model in isolation:

- new mouse frame/output/state definitions
- no dependency on old mouse AI plugin semantics

### Phase 3

Implement the state machine:

- `manual`
- `ads_entry_assist`
- `commit_hold`
- `reacquire_bridge`
- `switch_release`

### Phase 4

Remove or retire the old mouse assist path from the primary runtime.

## Recommendation

Proceed with this design.

The native C++ vision backend already provides enough underlying target information to support it, and the missing piece is narrow and explicit:

- preserve `target_source` through the controller boundary

Once that contract is expanded, the new mouse design can be implemented as a proper state machine instead of another round of generic mouse-correction tuning.
