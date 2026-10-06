# Upstream RE Findings to Seed Local Decompilation

This file records useful findings from the **current public libKCD2 master branch** that are newer/more descriptive than the project's pinned runtime dependency.

They are research seeds only. The mod remains pinned to its known-good libKCD2 commit until compatibility is explicitly reviewed.

## Horse yaw is a command, not world heading

Current upstream `S_HorseData.h` documents:

- `m_magnetYaw +0x130` as the raw yaw command;
- `m_yawSmoothed +0x128` as a SmoothCD state chasing `m_magnetYaw`;
- `m_yawVel +0x12C` as the damper velocity;
- `m_yawSmoothed` is explicitly **not world heading**;
- the smoothing path references `sub_18059B800`;
- a region around `0x180A4FDF1` zeroes/reset behavior on sign flip / `m_pseudoSpeed < 0.1`.

Implication: the final adapter should not interpret `m_magnetYaw` or `m_yawSmoothed` as a world-space yaw without decompiling the downstream application.

## Additional road-magnetism CVars

Current upstream maps the entire `S_HorseCVars` block, including:

- `RoadMagnetismMaxDelta`;
- `RoadMagnetismRoadCosLimit`;
- `RoadMagnetismFlickLimit`;
- `RoadMagnetismFlickMinAngle`;
- `RoadMagnetismInterruptTime`;
- `RoadMagnetismEnterAngle`;
- `RoadMagnetismRemainAngle`;
- `RoadMagnetismPathWidthCoeff/MinWidth/MaxWidth`;
- `RoadMagnetismSnapTime`;
- `RoadMagnetismCrossroadPredictionLen`;
- `RoadMagnetismBacktrackPathLength`;
- per-gait max-degree fields for run/sprint/dash;
- trend steer/width weights;
- separate OnPress and Auto road-distance on/off thresholds;
- `RoadMagnetismDeactivateTime`;
- `RoadMagnetismReactivateTime`;
- `RoadMagnetismDisableHintsVisibleTime`.

These names should be applied to decompiler reads by offset rather than left as anonymous floats.

## Mounted camera/view state

Current upstream maps `C_ActorPhysicsState` and documents:

- tick `0x1804415E0`, REL 26156;
- persistent view angles;
- mouse-look request term;
- additive `m_lookAngleAccum`;
- view-limit clamp `0x18053B508`;
- horse flat-yaw glue `0x1806CCAF8`, REL 37998, adding horse yaw delta to rider `m_lookAngleAccum`;
- camera centering `0x180A501E8`, REL 56442.

Current upstream also maps:

- `C_CameraRider::Compose` `0x1839C3748`, REL 434788;
- `C_CameraFirstPerson::Compose` `0x18094D030`, REL 51045.

This is directly relevant to decoupling Henry's travel orientation from mouse look.

## BetterHorseHandling is useful evidence, not vanilla logic

Current upstream `Projects/BetterHorseHandling` hooks the actor view-state tick at REL 26156 and implements a custom "turret compensation" by subtracting horse hull yaw delta from rider view yaw.

That code is mod logic, not vanilla, so it must not be copied as proof of the game's native camera algorithm.

It is still useful because it confirms:

- the mapped view-state hook is callable;
- rider view and horse hull yaw can be separated at that seam;
- current public offsets/types are operational enough for native experimentation.

## S_AutoController / S_OnPressController seeds

Current upstream preserves:

- `S_AutoController` factory `0x1829FC188`;
- vtable `0x183EAAE18`, REL 554715;
- `S_OnPressController` factory `0x181932168`;
- constructor `0x1819321F8`;
- primary vtable `0x183C34910`, REL 495794;
- secondary rider-state-machine-modifier vtable `0x183C348D0`, REL 495790.

Resolve the actual slot targets in IDA immediately; they are the shortest path to the exact controller algorithms.
