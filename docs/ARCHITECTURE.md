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
desired world-space road direction
        |
        v
world -> camera-local movement transform
        |
        v
normalized Henry movement input
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

## Camera independence

The mod should not rotate the camera just to make forward input point along the road.

Given desired road heading R and camera heading C:

```text
delta = R - C
moveX = sin(delta)
moveY = cos(delta)
```

Normalize the horizontal vector and inject it at full magnitude. Henry's downstream locomotion then decides walk/jog/sprint normally.

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
4. on-foot movement-vector injection;
5. integrated follow controller;
6. compatibility hardening;
7. release distribution.
