# Architecture

## Product behavior

The finished mod should make Henry automatically follow roads on foot using the same road-follow intelligence KCD2 uses for a mounted horse.

```text
KCD2 native road system
        |
        v
road acquisition / road sample
        |
        v
desired road heading / steering correction
        |
        v
Henry on-foot heading / turn seam
        |
        +--------------------+
        |                    |
        v                    v
sustained forward input   vanilla on-foot camera/facing coupling
        |
        v
vanilla on-foot locomotion
walk / jog / sprint / stamina / collision / animation
```

## Reuse boundary

Reuse:

- native road acquisition;
- native segment/candidate information;
- native road tangent/heading semantics;
- vanilla fork/continuation behavior where practical.

Do not transplant:

- horse gait/acceleration;
- horse collision avoidance;
- horse jump logic;
- horse slope handling;
- horse animation/bridle state;
- horse physics.

## On-foot camera and movement model

KCD2's on-foot camera and Henry's facing/travel direction are not independent in the way horse travel and camera look direction can be.

AutoWalk therefore must **not** create a special decoupled camera mode.

The intended control model is:

1. native road-follow logic provides the desired road heading / steering correction;
2. the mod steers Henry through an appropriate on-foot heading/turn seam;
3. the mod sustains ordinary forward movement, equivalent in intent to holding `W`;
4. KCD2's existing on-foot camera/facing behavior is allowed to respond normally;
5. Caps Lock / Shift and downstream locomotion continue to choose walk, jog, or sprint.

There is no world-to-camera movement-vector transform in the target architecture, and the mod should not synthesize lateral movement merely to keep the camera fixed while Henry changes road direction.

## Native system map

```text
C_Player
  └─ C_RiderPlayerControl
       └─ C_RiderPlayerInput

C_Horse
  └─ S_HorseData
       ├─ S_HorseRoadFollow
       │    └─ I_MagnetismController
       │         ├─ S_OnPressController
       │         └─ S_AutoController
       ├─ m_magnetYaw
       ├─ m_magnetismLive
       └─ m_magnetHit
```

## Phases

1. deterministic build/install and diagnostics;
2. mounted road instrumentation;
3. direct on-foot road sampling;
4. on-foot forward-input and heading/turn seam;
5. integrated follow controller;
6. compatibility hardening;
7. release distribution.
