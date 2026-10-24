# OptiFpsOverlay.asi

OptiScaler's FPS overlay as a standalone plugin. It does not need anything from OptiScaler's DLL, so it works with any OptiScaler v10 build (and without OptiScaler, with any ASI loader).

## Install

1. Put `OptiFpsOverlay.asi` in OptiScaler's `plugins` folder and set `LoadAsiPlugins=true` in `OptiScaler.ini` (not `-loadlate`: the plugin has to be loaded before the game creates its swapchain).
2. Turn OptiScaler's own overlay off (`ShowFps=false`), or both are drawn.
3. In the game: `End` shows or hides the overlay, `Home` opens the settings.

Settings are saved in `OptiFpsOverlay.ini` next to the plugin (edits to the file are picked up while the game runs), the log is `OptiFpsOverlay.log`. `ToggleKey` and `SettingsKey` are virtual-key codes (`0x23` is End, `0x24` is Home).

## What it shows

Rows measured by the plugin: FPS (average, 1% and 0.1% low), frame time (with graph), refresh rate, VRAM, GPU, CPU and RAM.

The settings are the ones of OptiScaler's overlay, with the same names, ranges and defaults: position, type, rows and their order, fonts, sizes, colors, shadow and outline, update interval, presets. A settings code (`OptiOverlay1:`) copied from one works in the other.

## What it cannot show

The rows that read OptiScaler's state have no data without it: upscaler and ratio, upscaler time, PC latency, bottleneck, frame generation names. They stay in the order list so a settings code keeps its order, and are not drawn.

## How it works

The plugin hooks `CreateSwapChain*` of the system's DXGI to learn the game's swapchain and command queue, then `Present`. It draws with its own copy of ImGui on the back buffer just before the frame is shown (Direct3D 12 on the swapchain's queue, Direct3D 11 on the immediate context). Vulkan games are not supported.

The telemetry (`OptiScaler/menu/overlay_telemetry.h`) and the ImGui backends are the ones of OptiScaler, built against the small stand-ins in `shim/`.

## Build

```
msbuild asi/FpsOverlay/FpsOverlay.vcxproj /p:Configuration=Release /p:Platform=x64
```

The result is `asi/FpsOverlay/bin/Release/OptiFpsOverlay.asi`.
