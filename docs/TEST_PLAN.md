# Test Plan

## 0. Reproducible build

```powershell
.\scripts\bootstrap.ps1
.\scripts\build.ps1 -Configuration Debug
.\scripts\package.ps1 -Configuration Debug
```

Pass:

- pinned libKCD2 verified;
- dependencies resolve;
- DLL/package generated.

## 1. Installation safety

Only `<game>/Mods/kcd_autowalk` may change.

## 2. Native mounted baseline capture

Before judging the on-foot version, record vanilla horse behavior on the same road set:

- E-hold engagement timing;
- straight road;
- shallow/medium/sharp curves;
- road edge/off-center entry;
- fork;
- intersection;
- short WASD corrections;
- sustained W/A/S/D deviation until magnetism falls off;
- release WASD before disengagement and observe resume;
- mouse free-look while following;
- maximum left/right/up/down view angles;
- camera behavior as the horse turns under a stationary mouse view.

Log native:

- `m_magnetismLive`;
- `m_magnetYaw`;
- `m_yawSmoothed`;
- `m_yawVel`;
- `m_roadFollow.m_latched`;
- `m_roadFollow.m_stick`;
- auto-controller state/path buffers where safely mapped;
- rider `m_move`, `m_turn`, `m_stickMag`.

This baseline is the acceptance oracle.

## 3. Native controller reconstruction

Using IDA/decompiler + runtime probes, verify:

- `S_HorseRoadFollow::Tick` full call flow;
- `S_AutoController::Tick`;
- `SetHoldLatched`;
- `GetRoadDistance`;
- enter/remain/deactivate hysteresis;
- crossroad/backtrack/path-vector behavior;
- manual-input interruption/falloff;
- output path into `m_magnetYaw` and smoothing.

Pass: a synthetic facade produces the same controller state/output sequence as a mounted horse when fed equivalent pose/speed/input.

## 4. Activation behavior

On foot, near a valid road:

- tap E briefly: should not incorrectly latch if vanilla requires a hold;
- hold E: acquire using native-equivalent timing;
- hold E off-road: fail naturally;
- engage while facing each road direction;
- engage near fork.

Pass: engagement feels like mounted vanilla, not a custom toggle.

## 5. Autonomous movement / free-look

With follow active and no WASD:

- Henry follows straight/curved roads;
- move mouse left/right/up/down during travel;
- keep mouse looking to the side while Henry turns;
- reach mounted-equivalent yaw limits;
- verify no 360 spin;
- verify camera is not forcibly whipped toward the road;
- verify body/travel direction continues following native road output.

Pass:

- travel and view frames are independent during autonomous follow;
- limits match the horseback camera behavior;
- no synthetic mouse steering is used to make Henry follow the road.

## 6. Manual WASD handoff

While follow is active:

### Short intervention

- press W/A/S/D briefly;
- movement instantly becomes normal on-foot camera-relative manual control;
- release before native magnetism deactivates.

Pass: autonomous follow resumes without re-holding E if native latch remains alive.

### Sustained intervention

- hold steering/movement long enough to leave the road or exceed native interruption thresholds.

Pass: native magnetism falls off naturally; releasing WASD does not reassert AutoWalk.

### Mixed input

- diagonal WA/WD/SA/SD;
- Shift;
- Caps Lock;
- quick taps vs sustained input.

Pass: no input fighting, no stuck synthetic state.

## 7. Locomotion preservation

Verify:

- Caps Lock walk;
- normal jog;
- Shift sprint;
- stamina drain;
- collision;
- stairs/slopes;
- combat transition;
- interaction;
- ladder;
- mount/dismount;
- save/load.

## 8. Camera-limit parity

Compare on-foot AutoWalk directly with horseback follow at the same location:

- max left/right yaw;
- max up/down pitch;
- recenter behavior if any;
- body turn beneath camera;
- mounted camera smoothing characteristics that should/should not be reproduced.

Do not accept arbitrary hardcoded limits merely because they feel reasonable.

## Release gate

No release until:

- controller output closely matches mounted vanilla;
- camera never whips to steer;
- free-look works within mounted-equivalent limits;
- manual WASD handoff/resume/falloff matches the native interaction;
- unsupported builds fail closed;
- disabling cannot leave stuck movement or camera state.
