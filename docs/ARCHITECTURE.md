# Architecture

## Product behavior

The target is not merely "walk Henry toward a sampled road tangent."

The target is:

> **Mounted KCD2 road-follow behavior, translated to Henry's on-foot locomotion and camera.**

## Target state machine

```text
                         on foot, near road
                                |
                           hold E
                                |
                                v
                    native follow acquisition
                                |
                                v
                      FOLLOW LATCHED / ACTIVE
                                |
            +-------------------+-------------------+
            |                                       |
        WASD neutral                           WASD active
            |                                       |
            v                                       v
 native road controller                    manual movement
 owns travel heading                       owns travel direction
            |                                       |
 Henry auto-forward                        normal camera-relative
            |                              on-foot movement
            |                                       |
 camera free-look                          camera behaves normally
 mounted-style limits                      for manual movement
            |                                       |
            +-------------------+-------------------+
                                |
                    native magnetism decides
                     whether latch survives
                                |
                    +-----------+-----------+
                    |                       |
                 survives                disengages
                    |                       |
          release WASD resumes      remain manual until
             native follow             E held again
```

## Critical separation: travel frame vs view frame

During autonomous follow, Henry needs two orientations:

1. **travel/body frame** — driven by the native road-follow controller;
2. **view/camera frame** — driven by mouse look within mounted-style limits.

The current experimental implementation incorrectly uses mouse X to rotate the camera so Henry's on-foot forward vector points down the road. That couples the two frames and causes the forced camera whipping.

The replacement implementation must not use camera rotation as the steering actuator.

## Native mounted pipeline to recover

Existing public RE already establishes:

```text
C_RiderPlayerInput
    m_move
    m_turn
    m_stickMag

S_HorseData
    m_pseudoSpeed
    m_magnetYaw
    m_yawSmoothed
    m_yawVel
    m_magnetismLive
    m_magnetHit
    m_roadFollow

S_HorseRoadFollow
    m_stick
    m_pathA
    m_pathB
    m_latched
    m_pMagnetism
    m_mode

I_MagnetismController
    SetHoldLatched()
    Tick(sample, phase, dt)
    GetRoadDistance()

S_AutoController
    persistent path/state
    Tick(...) through I_MagnetismController
```

The CVar surface shows that vanilla includes logic for:

- enter/remain angles;
- interrupt timing;
- acquire/deactivate distances;
- crossroad prediction;
- backtrack path length;
- path-width scoring;
- snap timing;
- trend steering and width weighting;
- flick handling;
- speed-dependent maximum steering.

These behaviors should come from the game, not from new AutoWalk constants.

## Camera pipeline to recover

Current libKCD2 RE identifies:

- `C_CameraRider::Compose` — mounted camera compose, REL 434788;
- `C_CameraFirstPerson::Compose` — on-foot first-person compose, REL 51045;
- `C_ActorPhysicsState` look-angle integrator and view-limit clamp;
- horse flat-yaw → rider `m_lookAngleAccum` glue — REL 37998;
- mounted camera centering — REL 56442;
- horse CVars:
  - `wh_horse_ViewLimitWide`;
  - `wh_horse_ViewLimitNarrow`;
  - `wh_horse_ViewLimitBottom`;
  - `wh_horse_CameraCentering*`;
  - `wh_horse_CameraCenteringMagnetismDegreeLimit`.

The exact mounted limit setup/selection must be recovered rather than approximated with guessed angles.

## Dismounted adapter goal

Preferred architecture:

```text
Henry IEntity position/orientation/velocity
                |
                v
      synthetic/adapted horse state
                |
                v
      REAL S_HorseRoadFollow
                |
                v
       REAL S_AutoController
                |
                v
     native desired road steering
                |
                v
       DISMOUNTED TRAVEL ADAPTER
                |
        Henry body/travel heading
                |
         on-foot locomotion
```

Parallel camera path while WASD-neutral:

```text
mouse input
    |
    v
Henry view state
    |
mounted-equivalent yaw/pitch limits
    |
free look independent of body/travel heading
```

When WASD becomes non-neutral, bypass the autonomous travel adapter and let normal on-foot movement own travel direction. Do not automatically destroy the native magnetism state.

## Engagement

The desired UX is the vanilla horseback interaction:

- hold E near a suitable road;
- native-style acquisition/latching;
- release E after engagement;
- following persists until native disengagement conditions are met.

If possible, reuse the same mounted input action/state-machine logic rather than implementing an arbitrary new timer.

## Reuse boundary

Reuse/translate:

- road acquisition;
- controller state;
- road/path candidate progression;
- fork behavior;
- path trend and width logic;
- steering output;
- manual-input interruption behavior;
- camera limit semantics.

Do not transplant:

- horse gait/acceleration;
- horse collision avoidance;
- horse jump/slope physics;
- horse animations/body geometry.

## Development phases

1. preserve the current prototype as a diagnostic baseline;
2. decompile and type the complete `S_HorseRoadFollow::Tick` + `S_AutoController` path;
3. decompile E-hold activation/latching and rider-WASD interruption;
4. decompile mounted camera compose/view-limit installation;
5. implement a native-controller synthetic facade;
6. implement a Henry travel/body actuator independent of camera yaw;
7. implement mounted-equivalent view limits while autonomous;
8. integrate manual WASD handoff/resume;
9. compare frame-by-frame against mounted vanilla behavior;
10. compatibility/release hardening.
