# Architecture

## Core principle

The final mod should not imitate KCD2 horse road following.

It should **recover the actual mounted controller and adapt its output to Henry**.

The current experimental follower only proves that native roads can be queried, forward input can be synthesized, and Henry can be turned programmatically. Its custom steering is not an architectural foundation.

## Source of truth

For mounted road-follow behavior, the source of truth is the decompiled `WHGame.dll` implementation.

Not video, manually sampled telemetry, guessed constants, hand-tuned control laws, or intuition about what the horse seems to do.

Runtime instrumentation is used only to validate the decompiler reconstruction.

## Target native pipeline

```text
rider input / hold-E state
          |
          v
S_HorseRoadFollow::Tick
          |
          +--> road query / sample
          |
          v
I_MagnetismController
          |
          +--> S_OnPressController
          |          or
          +--> S_AutoController
                     |
                     v
       persistent road/path state
                     |
                     v
            desired magnet yaw
                     |
                     v
          native yaw smoothing
                     |
                     v
            horse turn request
```

Every branch between those boxes is to be decompiled before the final on-foot controller is designed.

## Dismounted adaptation boundary

Preferred end state:

```text
Henry pose / speed / manual input
             |
             v
 adapter satisfying native controller inputs
             |
             v
 original recovered road-follow state machine
             |
             v
      native desired travel heading
             |
             v
 Henry body/travel-direction actuator
             |
             v
       normal human locomotion
```

If the original controller can execute directly against a synthetic `S_HorseData`/road-follow facade, prefer that.

If a portion cannot execute safely, translate the decompiled logic branch-for-branch rather than inventing a substitute.

## Camera architecture

The same decompiler-first rule applies to camera behavior.

Recover:

- `C_CameraRider::Compose`;
- comparison with `C_CameraFirstPerson::Compose`;
- horse-yaw -> rider look accumulator glue;
- view-limit channel installation/reference frame;
- mounted camera centering;
- any magnetism-specific camera behavior.

The target autonomous state has separate Henry body/travel orientation and player view orientation. The camera is not the steering actuator.

## Manual input architecture

Recover exactly how rider `m_move`, `m_turn`, `m_stickMag`, follow-stick state, interruption timers, and controller latch interact.

Then reproduce the same ownership transition on foot:

- manual input takes immediate control;
- native controller state either survives or deactivates according to its own logic;
- release of WASD permits resume only when that native state survives.

## Hold-E engagement

Recover the native mounted activation path from input binding to latch.

Do not invent a custom hold timer unless the native path truly cannot be reused.

## Development order

1. decompile all functions listed in `docs/DECOMPILATION_PLAN.md`;
2. type/name the full road-follow state machine;
3. recover exact manual-input interruption behavior;
4. recover exact rider-camera behavior;
5. determine the minimum synthetic horse facade required;
6. execute native controller against the facade if safe;
7. otherwise translate only the irreducible native logic;
8. implement Henry travel/body actuator;
9. implement mounted-equivalent camera decoupling/limits;
10. perform full-rate differential runtime validation.

No additional tuning of the prototype controller should occur before steps 1-4 are complete.
