# MW4 Audio Direction Visualization Research

Date: 2026-09-01

Status: research conclusion; no production audio code is enabled.

## 1. Objective

The product target is not louder footsteps. It is an external, visual-only sound
indicator similar to the mobile Delta Force presentation:

```text
rendered game audio
  -> detect a relevant sound activity
  -> recover its coarse direction
  -> publish an event
  -> draw a short-lived directional symbol in Fusion Canvas
```

The post-processor has no game-engine event metadata. It therefore cannot equal
an engine-native system that already knows the event class, owner, world
position, occlusion path and listener-relative azimuth.

## 2. Research Conclusion

This direction is feasible, but the preferred input is **the game's discrete
5.1/7.1 render stream before headphone downmix**, not final stereo audio.

The strongest first implementation is:

1. capture the MW4 process tree with WASAPI process loopback;
2. preserve `WAVEFORMATEXTENSIBLE` channel count and channel mask;
3. run an overlapping STFT/filter bank;
4. apply game-specific frequency weights for each requested sound profile;
5. calculate a direction-energy distribution from the weighted per-channel
   energy;
6. gate and hold accepted events;
7. publish sector activity to a separate `AudioFusionChannel`;
8. let the already event-driven `fusion_canvas.exe` render the symbols.

This is materially simpler than BirdNET-style semantic audio recognition. It is
also a much closer match to existing commercial sound-radar implementations.

## 3. Direct Precedents

### 3.1 RARA Audio is an enhancement clue, not the target implementation

RARA publicly advertises Equalizer APO, 7.1 surround and quieter gunshots. That
supports the hypothesis that its main product path is game-specific filtering,
channel processing and dynamic-range control. Those profiles are useful as a
way to discover frequency regions of interest, but RARA does not publicly claim
to produce event-classified visual directions.

### 3.2 Nahimic Sound Tracker is the closer product analogue

MSI's current Sound Tracker documentation says that it converts game sounds
such as footsteps and gunfire into on-screen direction indicators. More
importantly, its FAQ explicitly says that direction relies on 5.1 or 7.1 game
output and recommends Home Theater or Multichannel Speakers rather than normal
two-channel stereo.

Older MSI documentation is even more direct: Sound Tracker captures the 5.1 or
7.1 streams, shows dynamic directional segments, and maps opacity to sound
strength.

### 3.3 ASUS Sonic Radar confirms frequency-profile filtering

The ASUS Sonic Radar III manual exposes selectable radar-signal profiles for
vehicles, explosions, gunshots and footsteps on a frequency-band display. The
manual describes the radar as a visualization of sound activity by position and
the 3D pointer as the direction of the strongest current audio source.

This is strong evidence that a useful first version does not require complete
semantic sound recognition. A selected, game-tuned frequency profile can drive
a directional energy radar directly.

### 3.4 The A-Volute/Nahimic patent describes the algorithmic shape

The public A-Volute patent describes this pipeline:

- time-frequency transformation of every input channel;
- sound-activity energy for every frequency sub-band and channel;
- a direction vector using each channel's known speaker position;
- per-profile frequency weights;
- accumulation into angular subdivisions for display;
- multiple frequency weight sets for visually distinguishing sound types.

The patent gives a 50 ms analysis window as an example. It is useful evidence
for feasibility and experimental design, but it is not a freedom-to-operate
opinion. A distributed/commercial implementation needs an IP review and should
not clone claim language or a proprietary implementation.

## 4. Proposed Direction Estimator

Let `c` be an input channel, `b` a frequency band and `u_c` the unit vector for
the channel's speaker position. Ignore LFE for direction.

```text
E[c,b,t] = energy of channel c in band b at time t

v[b,t] = sum_c(E[c,b,t] * u_c)
          / (sum_c(E[c,b,t]) + epsilon)

class_vector[k,t] = sum_b(profile_weight[k,b] * band_activity[b,t] * v[b,t])
```

`atan2(class_vector)` gives the dominant azimuth. Its magnitude is a spatial
coherence value: a strong single direction produces a longer vector, while
diffuse or contradictory energy trends toward zero.

For the MVP, map the result into eight bins:

```text
Front, FrontRight, Right, RearRight,
Rear, RearLeft, Left, FrontLeft
```

Do not interpolate fake precision merely to make the icon move smoothly. A
sector remains visible only while its event and spatial confidence both pass.

## 5. Frequency Profiles Are Internal Sensors

RARA-style EQ should not modify the user's playback audio in this feature. Use
the same idea as parallel internal analysis profiles:

```text
raw multichannel audio
  +-> footstep-weighted filter bank -> footstep activity/direction
  +-> gunshot-weighted filter bank  -> gunshot activity/direction
  +-> vehicle-weighted filter bank  -> vehicle activity/direction
```

Each profile is a vector of weights, not one hard band-pass. MW4 surface,
distance, occlusion and equipment variations make a single fixed frequency
band too brittle.

The first profile should target footsteps only. It can produce a
`FootstepCandidate` symbol, but the UI and telemetry must not call it a proven
enemy footstep until controlled MW4 evaluation establishes that claim.

## 6. Ownership Rejection

Engine-native indicators can suppress the player's own events from metadata.
Post-processing cannot recover ownership reliably from audio alone. This
project has two useful non-audio signals:

- right-trigger/AutoFire timing can veto the player's own gunshot interval;
- left-stick locomotion and cadence can lower confidence for centered,
  periodic self-footstep candidates.

These signals should be used only as rejection/gating evidence. They must not
create an audio event, and they must not give audio any aim, fire or controller
authority.

Center dominance and amplitude are weaker fallbacks. They will fail when an
enemy is directly ahead or when the player's own sound is spatialized away
from center.

## 7. Capture Paths

### Path A - Preferred: preserved multichannel process loopback

Use `ActivateAudioInterfaceAsync` with
`AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`, include the target process tree,
and request/test an eight-channel float format. Preserve the returned channel
mask and record per-channel activity diagnostics.

This path is accepted only if a live probe proves that MW4 contributes distinct
front/side/rear channel signals. The API's ability to accept a multichannel
format does not prove that lost source direction can be reconstructed after the
game or endpoint has already produced stereo.

### Path B - Conditional: 7.1 endpoint before headphone virtualization

If process loopback returns only stereo, expose a 7.1-capable render endpoint
to the game and place analysis before the final headphone downmix. A physical
or virtual 7.1 endpoint/APO may provide this position, but it adds setup,
latency, reliability and compliance costs. It is a separate go/no-go decision,
not an automatic dependency for the MVP.

### Path C - Degraded: final stereo

Stereo balance can support a low-confidence `Left / Center / Right` display.
The Sound V2 product publicly describes this minimal approach as checking which
stereo channel is louder. It does not recover a trustworthy 360-degree front/
rear direction.

An in-game `Home Theater` mix preset is not evidence that Windows is receiving
5.1/7.1 PCM. The capture probe must verify the actual channel count and channel
mask; otherwise the path is stereo regardless of its marketing or menu label.

Bandwise ILD and HRTF spectral cues can be researched later, but front/back
confusion is expected. GCC-PHAT is not a microphone-array solution here because
the input is an already rendered game mix.

## 8. Current MW4 Replay Evidence

The supplied replay
`E:/迅雷下载/Replay 2026-08-30 20-57-51.mp4` cannot be used for this audio study.

Measured container properties:

```text
duration:       13.466667 s
audio codec:    AAC-LC
sample rate:    48000 Hz
layout:         stereo
reported rate:  about 2 kb/s
```

FFmpeg `astats` reports zero minimum, maximum, RMS, entropy and zero crossings
for both channels across all 646,144 decoded samples. The audio track is
entirely silent. This is an evidence limitation, not an algorithm failure.

## 9. Runtime and Fusion Integration

Keep audio outside the native Vision/controller hot path:

```text
audio_direction.exe
  -> AudioFusionChannel (single writer, latest state + event notification)

cod_native_runtime.exe
  -> existing Vision Fusion channel

fusion_canvas.exe
  -> waits on both channel events
  -> composes vision point marker and audio direction symbols
```

Do not extend the current vision-specific `FusionSlot` with high-rate PCM or
DSP features. Publish only presentation state such as:

```cpp
struct AudioSectorEvent {
    uint64_t onset_qpc;
    uint64_t expires_qpc;
    uint32_t event_class;
    uint32_t direction_bin;
    float event_confidence;
    float direction_confidence;
    float intensity;
};
```

The canvas is already event-driven, so an audio channel event can wake it
without a 60 Hz polling loop. A short hold/fade belongs to presentation state;
it must not feed back into detection.

## 10. Research Gates

### G0 - Capture truth

- process-tree capture attaches to MW4;
- actual sample format, channel count and channel mask are logged;
- a controlled front/side/rear sound produces distinct nonzero channel energy;
- no raw audio is persisted unless an explicit calibration recording is made.

If G0 returns stereo only, the 360-degree MVP is blocked until Path B is
explicitly accepted. Do not hide this with a more elaborate stereo estimator.

### G1 - Synthetic direction

- generated 5.1/7.1 channel sweeps map to the correct eight sectors;
- LFE never produces direction;
- equal diffuse energy suppresses direction;
- two opposed equal sources become ambiguous rather than an arbitrary winner;
- capture-to-publish p95 target is below 50 ms.

### G2 - Controlled MW4 calibration

Record repeatable samples with known relative direction for:

- enemy footsteps on several surfaces and distances;
- player's own footsteps while moving and stopping;
- player's own and enemy gunfire;
- reloads, explosions, voice, music and UI negatives;
- isolated events and common overlap cases.

Freeze profile, map, audio settings, endpoint layout and direction labels before
evaluating candidate thresholds.

### G3 - Offline acceptance

Report, per event profile:

- accepted direction accuracy and adjacent-sector accuracy;
- unknown/suppressed coverage;
- event precision/recall and false positives per minute;
- self-event rejection;
- onset-to-publish p50/p95/max;
- overlap and opposite-direction failure cases.

Only accepted high-confidence events should render. Coverage may be low in the
first version; incorrect high-confidence indicators are more harmful than a
missing indicator.

### G4 - Live Fusion acceptance

- marker position and icon remain stable during the event hold;
- no mouse/input interception;
- no Vision capture contamination;
- no measurable native capture, inference or controller regression;
- Fusion/audio process exit and restart clear stale indicators.

## 11. Recommended Build Order

1. Build a read-only `audio_capture_probe.exe` that reports process format,
   channel mask, per-channel RMS and packet/QPC latency.
2. Build WAV/synthetic replay for eight-channel direction-energy tests.
3. Add one `footstep_candidate` spectral profile and an offline evaluator.
4. Collect controlled MW4 multichannel samples; do not tune from the silent
   N-card replay.
5. Add the separate `AudioFusionChannel` and a mock direction symbol.
6. Enable live footsteps only after G0-G3 pass.
7. Add a small classifier only if frequency-profile gating cannot meet the
   false-positive requirement. Do not start with BirdNET/YAMNet.

## 12. Product and Compliance Boundary

The feature remains external and visual-only:

- no game-process injection;
- no game memory reads;
- no swapchain/audio API hooks inside the game;
- no aim, fire, target-selection or recoil authority;
- no attempt to evade anti-cheat or game policy enforcement.

MSI currently warns that some games may treat Sound Tracker-style assistance as
affecting fair play and may restrict accounts. Technical externality does not
guarantee policy acceptance; supported use must be checked separately.

## 13. Sources

- RARA Audio: <https://raraaudio.com/>
- MSI Sound Tracker FAQ: <https://us.msi.com/faq/3873>
- MSI Nahimic software introduction:
  <https://www.msi.com/support/technical_details/NB_SW_Nahimic>
- ASUS ROG software manual, Sonic Radar III:
  <https://dlcdnet.asus.com/pub/ASUS/mb/SocketAM4/CROSSHAIR-VI-HERO/E12653_ROG_ROG_STRIX_Z200_Series_SW_EM_WEB_20170419.pdf>
- A-Volute directional sound activity patent:
  <https://patents.justia.com/patent/20140177844>
- Microsoft Application Loopback sample:
  <https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/>
- Microsoft WASAPI loopback recording:
  <https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording>
- Microsoft `WAVEFORMATEXTENSIBLE` channel mask:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/channel-mask>
- Microsoft X3DAudio overview:
  <https://learn.microsoft.com/en-us/windows/win32/xaudio2/x3daudio-overview>
- Sound V2 stereo-balance radar description:
  <https://crosshairx.gg/soundv2.html>
- Local prior design:
  `docs/archive/research/AUDIO_DIRECTION_PIPELINE.md`
- Current Fusion channel:
  `native/shared_fusion/fusion_channel.h`
