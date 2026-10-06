# Compatibility Policy

## Baseline

- libKCD2 pin: `10d20f28faba462c4bf98a01abb48225cc51bb91`
- upstream stated target: KCD2 Steam 1.5.6
- manifest support pattern: `1.5.6*`

## Why native compatibility is strict

KCD2 release builds may reshuffle virtual layouts. A native call correct for one build can be invalid on another.

## Current safety state

Current code:

- loads via the stable KCSE interface;
- logs raw KCSE/game/release identifiers;
- exposes read-only diagnostics;
- installs no mutation hooks.

Before the first mutation hook, require either:

1. validated Address Library `REL::ID`; or
2. documented signature scan with semantic validation and fail-closed behavior.

## Updating libKCD2

Do not follow upstream automatically. Review native layout changes, build, test load, rerun road probes, update docs, then change the pin.

## Unsupported update behavior

The release target is: no unsafe hooks, clear diagnostic, vanilla game continues. Never reuse an old raw RVA after WHGame.dll changes.
