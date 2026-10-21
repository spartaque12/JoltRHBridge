# JoltRHBridge

A tiny ASI plugin for **GTA San Andreas 1.0 US** that makes
**Jolt Physics** (by MrRealistic) and other
[plugin-sdk](https://github.com/dk22pac/plugin-sdk) mods, work together with
[gtaRenderHook](https://github.com/petrgeorgievsky/gtarenderhook).

## The problem

gtaRenderHook replaces the game's main frame function `Idle` (`0x53E920`) with its own
deferred renderer. plugin-sdk hooks two of its events at call sites *inside* the original
`Idle`:

| Event              | Call site  | Original target           |
|--------------------|------------|---------------------------|
| `gameProcessEvent` | `0x53E981` | `CGame::Process` `0x53BEE0` |
| `renderSceneEvent` | `0x53EABF` | `RenderScene` `0x53DF40`    |

With RenderHook active those call sites never execute. Jolt Physics loads, but its
update and render callbacks never run, so you get no ragdolls, tracers, gore or anything
else.

## What the bridge does

- **renderSceneEvent:** RenderHook still calls `Render2dStuff` through the call target
  at `0x53EB12`, which it reads every frame. The bridge hooks that call. When it's called
  from RenderHook (not from the original `Idle`), the bridge runs the `0x53EABF` hook
  chain with `RenderScene` temporarily patched to `ret`. Every subscriber gets its frame,
  and the scene isn't drawn twice.
- **gameProcessEvent:** RenderHook calls `CGame::Process` through function pointers it
  fills with `0x53BEE0` at startup. On the first RenderHook frame, the bridge points them
  at the `0x53E981` hook chain. That chain calls `CGame::Process` once, then every
  subscriber, exactly like the unmodded `Idle`.

Without RenderHook, or with its `UseIdleHook` off, the original `Idle` runs and the bridge
does nothing.

## Double get-up fix

With Jolt Physics, a ped knocked down (falling off a bike, for example) gets up twice, the
second time with a jolt. This happens with or without RenderHook:

1. The knockdown gives the ped the game's own `TASK_COMPLEX_FALL_AND_GET_UP` (208) in an
   event-response task slot.
2. Jolt ragdolls the body. When it settles, Jolt adds its own `TASK_SIMPLE_GET_UP` (205) through
   `CPedIntelligence::AddTaskPhysResponse` (`0x600DC0`). The physical-response slot has priority,
   so Jolt's get-up plays first.
3. When it ends, the game's fall task resumes in its lower slot and plays a second get-up.

The bridge hooks Jolt's call to `AddTaskPhysResponse`. When Jolt adds a get-up, the bridge
aborts and removes a waiting fall-and-get-up task from the event-response slots, so only Jolt's
get-up plays. It only patches the Jolt build it was traced on (the September 22 build) and logs
it if your build differs.

## Install

1. Install Jolt Physics. Put `JoltPhysics.SA.asi`, `.ini` and `.dat` in the **game
   folder**.
2. Put `JoltRHBridge.asi` (see [Releases](../../releases)) in the game folder too.
3. Start the game. `JoltRHBridge.log` next to the ASI should show something like:

   ```
   hooked Render2dStuff call at 0x0053EB12 (previous target 0x0053E230)
   JoltPhysics.SA.asi at 0x...: double get-up fix installed
   repointed 2 RenderHook CGame::Process pointer(s) to the 0x53E981 chain head 0x...
   first renderSceneEvent fired via chain head 0x...
   ```

To uninstall, delete `JoltRHBridge.asi`.

## Build

You need Visual Studio 2022 (or Build Tools) with the C++ x86 toolchain:

```bat
build.bat
```

This produces a 32-bit `JoltRHBridge.asi` with no dependencies beyond `kernel32`.

## Notes

- Tested with GTA SA 1.0 US, gtaRenderHook (D3D11), Jolt Physics September 22 build,
  Ultimate ASI Loader, modloader, CLEO 5 and SilentPatch.
- Other plugin-sdk mods that hook `gameProcessEvent` or `renderSceneEvent` (CLEO's
  `0x53E981` hook among them) get their events back too.

## License

MIT
