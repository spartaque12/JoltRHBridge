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

## Install

1. Install Jolt Physics. Put `JoltPhysics.SA.asi`, `.ini` and `.dat` in the **game
   folder**.
2. Put `JoltRHBridge.asi` (see [Releases](../../releases)) in the game folder too.
3. Start the game. `JoltRHBridge.log` next to the ASI should show something like:

   ```
   hooked Render2dStuff call at 0x0053EB12 (previous target 0x0053E230)
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
