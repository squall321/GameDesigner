# DesignPatternsSample

A minimal, runnable sample that **composes** several DesignPatterns plugin modules into one project,
to show how the pieces fit together. It is intentionally code-only and headless-safe so it doubles as a
smoke test of the plugin's subsystem wiring.

## What it demonstrates

`USampleGameInstance::Init` (in `Source/DesignPatternsSample/SampleGameInstance.cpp`) wires modules
together purely through their **public APIs and seams** — the modules never depend on each other:

- **World hub** (`DesignPatternsWorld`) — sets a global flag, bumps a counter, reads them back. Any
  system (quests, achievements, UI) reads the same hub the same way.
- **Save slots** (`DesignPatternsSaveSystem`) — lists existing save slots and the most-recent slot via
  the `ISeam_SaveSlotManager` seam (exactly how a "Continue" button would).

The module's `Build.cs` depends on a representative slice of the plugin
(`DesignPatterns`, `DesignPatternsSeams`, `DesignPatternsWorld`, `DesignPatternsSkillTree`,
`DesignPatternsProgression`, `DesignPatternsGameMode`, `DesignPatternsGameFlow`,
`DesignPatternsSaveSystem`, `DesignPatternsWorldSystems`). Adding more systems is just adding more
module names there — the seams keep everything decoupled.

## How to run it

1. Copy the **plugin** into this project's `Plugins/` directory:
   `DesignPatternsSample/Plugins/DesignPatterns/` (copy `../../Plugins/DesignPatterns`).
2. Right-click `DesignPatternsSample.uproject` → **Generate Visual Studio project files**.
3. Build **Development Editor** and open the project, or build from the command line (see the plugin's
   `BUILDING.md`). On launch, watch the log for the `LogDPSample` composition summary.

> `EngineAssociation` is set to `5.4`; change it to your installed engine (5.3–5.5) before generating
> project files.

## Folder layout

```
DesignPatternsSample/
├── DesignPatternsSample.uproject     # enables the DesignPatterns plugin
├── Config/DefaultEngine.ini          # points GameInstanceClass at the sample
└── Source/
    ├── DesignPatternsSample.Target.cs
    ├── DesignPatternsSampleEditor.Target.cs
    └── DesignPatternsSample/
        ├── DesignPatternsSample.Build.cs   # composes the plugin modules
        ├── DesignPatternsSample.cpp        # IMPLEMENT_PRIMARY_GAME_MODULE
        ├── SampleGameInstance.h
        └── SampleGameInstance.cpp          # the composition demo
```
