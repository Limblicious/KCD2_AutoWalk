# Test Plan

## 0. Build and install safety

```powershell
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Configuration Debug
.\scripts\package.ps1 -Configuration Debug
```

Only `<game>/Mods/kcd_autowalk` may be modified by install/uninstall.

## 1. Static reconstruction gate

Before further final-controller work, complete `docs/DECOMPILATION_PLAN.md`.

Required outputs:

- typed decompilation of `S_HorseRoadFollow::Tick`;
- typed decompilation of `S_AutoController::Tick`;
- typed decompilation of `S_OnPressController` activation/latch path;
- complete road sampler/state-builder call graph;
- exact rider-input interruption/falloff logic;
- exact desired-yaw and smoothing path;
- exact mounted camera/view-limit path.

Pass condition:

The controller's branches, fields, CVars, state transitions, and downstream outputs are documented well enough to implement without guessing.

## 2. Native-controller execution test

Preferred path: instantiate/supply the minimum synthetic state needed to run the native controller using Henry's pose.

Test controller state/output first; do not move Henry.

Pass:

- controller executes without a real mounted horse owning locomotion;
- native sample/path state advances;
- native desired yaw/output is produced;
- no guessed steering law is introduced.

If direct execution is impossible, document the exact dependency that blocks it before translating that portion.

## 3. Full-rate differential instrumentation

Runtime comparison is validation, not reverse engineering.

Instrument the original mounted controller and the synthetic/dismounted controller at the **same native update hook and cadence**, not through video or occasional console commands.

Capture every tick:

- `dt`;
- physical/manual input;
- latch/mode;
- complete road sample;
- controller persistent fields;
- path vectors;
- `m_magnetismLive`;
- `m_magnetYaw`;
- `m_yawSmoothed`;
- `m_yawVel`;
- recovered interruption timers/state.

Pass: mounted and synthetic state/output sequences agree within expected pose/floating-point differences.

## 4. Hold-E activation

Verify recovered native activation semantics on foot, including valid road, off-road, opposite direction, fork/intersection, and boundary hold durations.

## 5. Autonomous movement

With native controller active and WASD neutral:

- body/travel follows native output;
- forward locomotion remains human/vanilla;
- camera is not used for steering.

## 6. Camera parity

Mounted-style recenter milestone:

- engage on a straight road, move the mouse at least 90 degrees away, and
  verify mouse look remains immediately authoritative while input is present;
- release the mouse and verify the view blends smoothly toward the autonomous
  travel heading using the native `CameraCentering` rate and pitch offset;
- follow a long curve and verify the recenter target tracks travel without
  oscillation, snapping, or feeding back into steering;
- verify no forced camera whipping;
- open ESC, inventory, map, and another full-UI screen while offset from the
  travel heading; verify no camera delta is applied in UI and resume starts a
  fresh blend from the current view;
- test brief and sustained WASD, explicit deactivation, road loss, save/load,
  and mounting; verify no stale camera delta survives cleanup;
- trigger an interaction/focus-camera sequence during follow and verify the
  AutoWalk recenter yields until the native focus owner releases the view;
- ride normally with AutoWalk inactive and verify native horseback camera
  behavior is unchanged.

Separate target-relative view-limit gate:

- apply the same mounted view-limit semantics;
- verify free-look while Henry turns;
- verify no unrestricted 360 spin;
- verify clean restoration outside AutoWalk.

Use internal-state comparison where possible rather than estimating limits from recordings.

## 6b. Double-rotation experiment (movement request coordinate space)

Runtime data showed the measured displacement heading ≈ 2 × travelYaw while
the recenter locks the view. Candidate explanation: the movement consumer
applies the request velocity in a frame rotated by the reference yaw
(flat/entity/view/camera). The camera log now records, every ~1 s:

`travel` (commanded), `req` (request direction after override),
`reqVanilla` (before override), `flat` (C_ActorPhysicsState+0x44),
`view`, `ent` (entity world-TM), `cam` (rendered camera), `move`
(consecutive frame), `moveWin` (rolling ~0.5 s), raw `dxy`, `dt`,
`measSrc`, and signed error predictions in degrees:

- A: `move ≈ travelYaw`
- B: `move ≈ flatYaw + requestYaw`
- C: `move ≈ entityYaw + requestYaw`
- D: `move ≈ viewYaw + requestYaw`
- E: `move ≈ cameraYaw + requestYaw`

Three conditions:

1. straight road, camera fully recentered;
2. straight road, holding the view ~90° off the travel direction;
3. curved road, no mouse input.

Pass criterion: one prediction consistently near 0° error in all three
conditions; the others diverge when the view is turned away. Do not change
the movement vector until the consumer's coordinate space is confirmed.

## 7. Manual WASD handoff

Use recovered native interruption rules.

Test brief, sustained, diagonal, repeated, pre-threshold release, and post-deactivation release.

Pass:

- manual input owns movement immediately;
- follow resumes only when native state says it should;
- no hard-coded any-key cancel remains.

## 8. Locomotion preservation

Verify Caps Lock, jog, Shift sprint, stamina, collision, stairs/slopes, combat, interaction, ladder, mount/dismount, save/load.

## Release gate

No release until:

- the final controller is traceable to recovered native code;
- no custom tangent/CTE/PID replacement remains;
- camera never acts as steering actuator;
- manual input/latch behavior matches recovered native logic;
- unsupported builds fail closed.
