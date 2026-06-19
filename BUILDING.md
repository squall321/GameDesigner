# Building the DesignPatterns plugin (cross-platform)

This repository contains the **DesignPatterns** Unreal Engine 5 plugin at
`Plugins/DesignPatterns/`. A UE plugin compiles as part of a host project or via Epic's
`BuildPlugin` automation. Both paths are scripted in `Tools/build.sh`.

## Supported platforms

| Platform | Status | Notes |
|---|---|---|
| Windows (Win64) | ✅ | Primary development platform |
| macOS | ✅ | |
| Linux | ✅ | |
| Android | ✅ | Touch input + mobile perf tier + app suspend/resume via the Platform module |
| iOS | ✅ | As Android |
| Consoles (PS5 / Xbox / Switch) | ✅* | Compile-safe; `*` console-specific platform extensions are provided by the platform SDK, not this repo |

The plugin sets **no platform allow/deny list** in its `.uplugin`, so it is offered on every
platform. All platform-specific code lives in `DesignPatternsPlatform` behind `#if PLATFORM_*`
guards, each with a generic-desktop `#else` fallback, so the code compiles on platforms not
explicitly enumerated.

## Engine requirement

Unreal Engine **5.3 – 5.5** with the C++ toolchain:
- **Windows:** Visual Studio 2022 + *Game development with C++* workload.
- **macOS:** Xcode (matching the engine's required version).
- **Linux:** the engine's bundled clang toolchain.
- **Android/iOS:** the platform SDKs configured in the engine (Android Studio/NDK, Xcode).

## Option A — verify the plugin compiles for several platforms (no game needed)

This is the fastest cross-platform check. It runs `RunUAT BuildPlugin`, which compiles the plugin
in isolation for each listed platform:

```bash
# Windows (Git Bash):
ENGINE="/c/Program Files/Epic Games/UE_5.4" ./Tools/build.sh plugin Win64+Linux+Android

# macOS / Linux:
ENGINE="$HOME/UE_5.4" ./Tools/build.sh plugin Mac+Linux+IOS
```

Output goes to `Build/PluginPackage/` (git-ignored).

## Option B — build a host project that uses the plugin

1. Create or open a C++ UE project.
2. Copy `Plugins/DesignPatterns/` into `<YourProject>/Plugins/DesignPatterns/`
   (or add this repo as a submodule under the project's `Plugins/`).
3. Enable **Design Patterns** in *Edit → Plugins* and build:

```bash
ENGINE="/c/Program Files/Epic Games/UE_5.4" \
  ./Tools/build.sh project /path/to/YourProject.uproject Win64 Development
```

## Manual command equivalents

```bash
# Plugin package (cross-platform compile check):
"<Engine>/Engine/Build/BatchFiles/RunUAT.bat" BuildPlugin \
  -Plugin="<repo>/Plugins/DesignPatterns/DesignPatterns.uplugin" \
  -Package="<repo>/Build/PluginPackage" -TargetPlatforms=Win64+Linux+Android -Rocket

# Host project editor build:
"<Engine>/Engine/Build/BatchFiles/Build.bat" YourProjectEditor Win64 Development \
  -Project="<path>/YourProject.uproject" -WaitMutex -FromMsBuild
```

## Pre-build sanity check (no engine required)

Before a real build, run the bundled static checker — it catches the most common UHT/compile
mistakes (`.generated.h` ordering, module declarations, brace balance, and stub/placeholder/TODO
markers) without needing an engine install:

```bash
python Tools/dp_static_check.py
```

It exits non-zero on any error. It is **not** a substitute for a real engine build — always do
Option A or B before shipping.

## Opt-in GAS module

`DesignPatternsGAS` is excluded from the default `.uplugin` so projects without GAS compile. To
build it, enable the engine's `GameplayAbilities` plugin and add
`{ "Name": "DesignPatternsGAS", "Type": "Runtime", "LoadingPhase": "Default" }` to the `Modules`
array in `DesignPatterns.uplugin`.
