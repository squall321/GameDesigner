# DesignPatterns — Unreal Engine 5 General-Purpose Game Framework

A production-grade, reusable collection of game design patterns and genre toolkits for
**Unreal Engine 5 (5.3 – 5.5)**, built as a drop-in **plugin**. Everything is C++ core with
Blueprint exposure, designed for clean development, easy debugging, strong extensibility,
networked correctness, and cross-platform (PC / console / mobile) deployment.

> Drop the `DesignPatterns` plugin folder into your project's `Plugins/` directory, enable it,
> regenerate project files, and build. See **Building** below.

---

## 1. Module map (what lives where)

The plugin is split into focused modules so you only pay for what you use. Genre modules build on
the core; you can delete any genre module you don't need without touching the others.

| Module | Type | Purpose | Depends on |
|---|---|---|---|
| **DesignPatterns** | Runtime | Core patterns: subsystem singletons, message bus, object pool, service locator, FSM, strategy, command, factory, lightweight actions, data registry, save system | — |
| **DesignPatternsUI** | Runtime | MVVM ViewModel, view bases, UI mediator/layout subsystems, pub/sub UI listener | core |
| **DesignPatternsPlatform** | Runtime | Cross-platform layer: input device abstraction, performance/capability tiers, save-path helpers, app lifecycle (mobile suspend/resume) | core |
| **DesignPatternsCombat** | Runtime | Action/combat: health & damage, hitboxes, combos, status effects | core |
| **DesignPatternsRPG** | Runtime | RPG: replicated inventory (FastArray), equipment, stats/leveling, quests | core |
| **DesignPatternsSurvival** | Runtime | Survival/crafting: resource nodes, crafting, needs (hunger/thirst), day-night, durability | core, RPG |
| **DesignPatternsNet** | Runtime | Multiplayer: sessions/matchmaking, authority component, RPC validation flow, quantized replication helpers | core |
| **DesignPatternsDeveloper** | Runtime (Shipping-stripped) | `DP.*` console commands for live debugging | core, UI |
| **DesignPatternsEditor** | UncookedOnly | Gameplay-debugger category & editor tooling | core, UI |
| **DesignPatternsGAS** | Runtime (opt-in) | Bridge from the lightweight action system to GAS. Not enabled by default — see below. | core |
| **DesignPatternsTests** | UncookedOnly | Automation tests (`#if WITH_AUTOMATION_TESTS`) | core |

### 1a. High-level composable layer

Higher-level, genre-agnostic systems you **assemble** to build a game. Everything below couples to
everything else **only through interface seams** (`TScriptInterface`) owned by the `DesignPatternsSeams`
leaf module — so any genre mix composes and any module can be deleted without breaking the others.

| Module | Type | Purpose | Depends on |
|---|---|---|---|
| **DesignPatternsSeams** | Runtime | Shared interface seams + value types (`ISeam_SimClock/NeedProvider/Persistable/EntityIdentity/TileProviderRead/InputModeArbiter/AnalyticsSink/ActivationGate/TextToSpeech/AccessibilityConsumer`; `FSeam_NetValue` closed net variant, `FSeam_EntityId`, `FSeam_CellCoord`). The composition spine — every high-level module depends only on this. | core |
| **DesignPatternsEntity** | Runtime | Composable entity-trait / capability system (ECS-flavored): stable identity + data-driven trait set, capability queries, archetype data assets | core, Seams |
| **DesignPatternsWorld** | Runtime | Central data hub / world state: tag-keyed flags/variables/counters, scoped (global/faction/entity) blackboards, query layer, save + optional replication | core, Seams |
| **DesignPatternsSimGrid** | Runtime | Grid/tile world + placement: chunked grid (delta-replicated cells), placement rules/validation, spatial queries, territory | core, Seams |
| **DesignPatternsInteraction** | Runtime | General interaction: `IInteract_Interactable`, interactor component (server-validated), focus targeting, data-driven verbs | core, Seams |
| **DesignPatternsSimEconomy** | Runtime | Simulation economy: commodities/stockpiles, production/consumption chains, market price formation, trade | core, Seams |
| **DesignPatternsSimAgents** | Runtime | Sim agents: sim clock, schedules, generalized needs, utility-AI brain (on core Strategy), job board, crowd steering | core, Seams, Entity |
| **DesignPatternsInventoryUI** | Runtime | Generalized inventory/grid **window** UI (bags, crafting grids, hotbars, equipment, shops, chests) bound to any backend via `IInvUI_ItemContainer` | core, UI, Seams |

### 1b. Reactive layer (Wave-1)

Genre-agnostic reactive systems that **wrap** the engine's strong subsystems and react to gameplay via
the message bus / seams. Almost entirely local/cosmetic; the few authoritative bits feed the World hub.

| Module | Type | Purpose | Depends on |
|---|---|---|---|
| **DesignPatternsAudio** | Runtime | Tag-keyed sound manager/buses, adaptive layered music (state model), mix snapshots, ambience | core, Seams |
| **DesignPatternsCamera** | Runtime | Priority/blend camera-mode stack, modes-as-strategy, shake-by-tag, targeting/lock-on (wraps `APlayerCameraManager`) | core, Seams |
| **DesignPatternsAI** | Runtime | Advanced AI: perception adapter, behavior bridge (FSM or BehaviorTree), squad tactics, budget/wave spawn director (wraps AIModule) | core, Seams; World+SimAgents private |
| **DesignPatternsNarrative** | Runtime | Dialogue graph + runner, branching story director, cutscene/sequence control (wraps LevelSequence), condition/effect mini-language | core, Seams, World |
| **DesignPatternsHUD** | Runtime | HUD layout, notification/toast queue, minimap/markers, menu navigation stack, Enhanced Input context layering | core, UI, Seams, Platform |

### 1c. Infrastructure layer (Wave-2)

Cross-cutting infrastructure. Engine-wrapping, opt-in, privacy-safe.

| Module | Type | Purpose | Depends on |
|---|---|---|---|
| **DesignPatternsAnalytics** | Runtime | Tag-keyed event recording (consent-gated, PII-safe via `FSeam_NetValue`), bus→event mapping, progression metrics, A/B experiments (wraps `IAnalyticsProvider` via a seam) | core, Seams |
| **DesignPatternsLocalization** | Runtime | Localization (wraps FText/StringTable/culture), subtitles, accessibility options (colorblind/subtitle/UI-scale/shake/TTS) broadcast to consumers | core, UI, Seams |
| **DesignPatternsLevelDirector** | Runtime | Level streaming director, deterministic procedural placement (seeded, authority-only, via core Factory/Pool), spawn regions, activation gating | core, Seams |
| **DesignPatternsModContent** | Runtime | Mod/DLC content-pack pipeline: discover/mount/unmount packs, tag→asset override registry over the data registry, validate-before-activate | core, Seams |
| **DesignPatternsModContentEditor** | UncookedOnly | Data-validation commandlet hook (CI) + editor validation of content packs | ModContent |

### 1d. Gameplay + flow/meta layer (Wave-3)

Higher-level game systems you assemble into an actual game loop. Same composition rules: built on the
core + Seams, coupled only through seams, independently removable.

| Module | Type | Purpose | Depends on |
|---|---|---|---|
| **DesignPatternsSkillTree** | Runtime | Skill nodes/trees, replicated learned-skill component, ability-grant adapter (via the core Action component), point sources, save | core, Seams |
| **DesignPatternsProgression** | Runtime | Tag-keyed wallet/currency (`ISeam_Wallet`), achievements (conditions + analytics + platform bridge), shop (vendor + server-validated client) | core, Seams |
| **DesignPatternsGameMode** | Runtime | Ruleset/win-conditions, match-state FSM, replicated score carrier (`ISeam_ScoreSource`), teams (`ISeam_TeamAffinity`), respawn, scoreboard VM | core, Seams |
| **DesignPatternsGameFlow** | Runtime | App-flow FSM (Boot/Menu/Loading/InGame/Pause/Results), loading-screen wrapper, results VM, continue-slot | core, Seams |
| **DesignPatternsSaveSystem** | Runtime | Named-slot manager wrapping the core save subsystem (`ISeam_SaveSlotManager`), checkpoints, rotating autosave, migration steps | core, Seams |
| **DesignPatternsSaveSystemUI** | Runtime | The save/load slot ViewModel (isolates the UI dep so the save core builds on a dedicated server) | SaveSystem, UI |
| **DesignPatternsWorldSystems** | Runtime | Weather state model, pooled VFX manager (`ISeam_VfxController`), possession-based rideable/mount (`ISeam_Rideable`) | core, Seams |
| **DesignPatternsTutorial** | Runtime | Data-driven onboarding step director + contextual hints | core, Seams |
| **DesignPatternsReplay** | Runtime | Demo-driver wrapper, event timeline, playback scrubber, spectator | core, Seams |

A runnable composition of these modules lives in [`Examples/DesignPatternsSample`](../../Examples/DesignPatternsSample) —
a minimal project whose `USampleGameInstance` wires the World hub + save slots together through public
APIs and seams.

> **Composition guarantee:** none of the high-level / reactive / infrastructure modules hard-includes
> another sibling or any genre module. All cross-module coupling is through `DesignPatternsSeams`
> interfaces resolved from the service locator (or off an actor) and message-bus tags. Delete any one
> module and the rest still compile and run — the seam, not the provider, is the link dependency, and an
> unresolved seam degrades to a documented inert default (gate=open, sink=no-op, TTS=silent).

---

## 2. Folder classification (how to find things)

Every module follows the same layout, and every system lives in its own **functional subfolder** so
the codebase is easy to navigate and classify:

```
Source/<Module>/
├── <Module>.Build.cs           # module dependencies
├── Public/<Area>/*.h           # public headers, grouped by feature area
└── Private/<Area>/*.cpp        # implementations, mirroring the Public layout
```

Core (`DesignPatterns`) areas: `Core`, `MessageBus`, `Pool`, `Services`, `FSM`, `Strategy`,
`Command`, `Factory`, `Action`, `Data`, `Save`.

Genre/platform module areas:

| Module | Subfolders |
|---|---|
| Combat | `Health` · `Hit` · `Combo` · `Effect` |
| RPG | `Item` · `Inventory` · `Stats` · `Quest` |
| Survival | `Resource` · `Crafting` · `Needs` · `World` |
| Net | `Session` · `Replication` · `RPC` |
| Platform | `Input` · `Capability` · `Storage` · `Lifecycle` |
| UI | `MVVM` · `View` · `Mediator` · `PubSub` |

**Naming convention:** public types are prefixed per module — core `UDP_/FDP_/IDP_/EDP_`, Combat
`UCombat_/FCombat_/…`, RPG `URPG_/…`, Survival `USurv_/…`, Net `UNet_/…`, Platform `UPlat_/…`.
Native gameplay tags are anchored under `DP.*` (e.g. `DP.Bus.Combat.Death`).

---

## 3. Core systems (quick reference)

- **Subsystem singletons** — `UDP_GameInstanceSubsystem` / `UDP_WorldSubsystem` bases. No raw statics;
  the engine owns lifetime and GC. Access via `FDP_SubsystemStatics::GetGameInstanceSubsystem<T>(WCO)`.
- **Message bus** — `UDP_MessageBusSubsystem`. Tag-keyed pub/sub with tag-hierarchy matching, weak
  listener ownership (auto-prune), C++ and Blueprint listeners, deferred dispatch via the core ticker.
- **Object pool** — `UDP_ObjectPoolSubsystem` (world-scoped). Warmup, reclaim, soft-cap eviction,
  `IDP_Poolable` reset hooks. Timers fully owned and cancelled on teardown.
- **Service locator** — `UDP_ServiceLocatorSubsystem`. Tag-keyed, strong-owned or weak-observed
  lifetimes; weak entries auto-invalidate on GC.
- **FSM** — definition/runtime split: a shared `UDP_StateMachineDefinition` data asset + a lightweight
  `UDP_StateMachineComponent` that replicates only the active state tag (authority-driven).
- **Strategy / Command / Factory / Action** — pluggable, data-driven, designer-subclassable.
- **Data registry & Save** — tag-identified `UDP_DataAsset` + AssetRegistry index; versioned, async,
  migration-aware save system that serializes on the game thread and does IO on a worker.

---

## 4. Networking & authority (read this for multiplayer)

Every replicated component follows the same contract, enforced throughout the plugin:

- `SetIsReplicatedByDefault(true)` in the constructor.
- `GetLifetimeReplicatedProps` with `DOREPLIFETIME` for each replicated property, and an `OnRep_` for
  each `ReplicatedUsing`.
- **Every mutator of replicated / server-authoritative state guards on authority at the top of the
  function** (`if (!GetOwner() || !GetOwner()->HasAuthority()) return;`). Clients never mutate
  authoritative state directly — they mirror it via OnRep.
- `FInstancedStruct` is never sent through a plain replicated property; the inventory uses a proper
  `FFastArraySerializer`.

`DesignPatternsNet` adds session management, a canonical Server-RPC → validate → apply → multicast
flow (`UNet_AuthorityComponent`), and helper utilities.

---

## 5. Cross-platform (PC / console / mobile)

`DesignPatternsPlatform` keeps all platform branching in one place so the rest of the plugin stays
platform-agnostic:

- **Input** (`UPlat_InputRouterSubsystem`) — current device (keyboard-mouse / gamepad / touch),
  `OnInputDeviceChanged`, `IsTouchPlatform()` / `IsGamepadActive()`.
- **Capability** (`UPlat_DeviceCapabilitySubsystem`) — a performance tier (Low/Medium/High/Ultra) and
  feature flags (touch, rumble, handheld, mobile, console) to drive scalability.
- **Storage** (`UPlat_StorageLibrary`) — correct per-platform save directory and slot-name sanitisation
  (feed this into the core Save system).
- **Lifecycle** (`UPlat_AppLifecycleSubsystem`) — `OnAppSuspended` / `OnAppResumed` from the engine's
  application delegates (mobile suspend/resume, console constrained mode) for auto-pause / auto-save.

---

## 6. Debugging

- **Log categories** — `LogDP`, `LogDPBus`, `LogDPPool`, `LogDPFSM`, `LogDPCmd`, `LogDPService`,
  `LogDPData`, `LogDPSave`, `LogDPFactory`, `LogDPAction`. Tune per system, e.g. `Log LogDPBus Verbose`.
- **Stat group** — `stat DesignPatterns`.
- **Console commands** (non-Shipping) — `DP.Bus.DumpListeners`, `DP.Pool.Stats`, `DP.FSM.LogState`,
  `DP.Service.List` / `DP.Service.Resolve`, `DP.Cmd.Dump/Undo/Redo`,
  `DP.Data.RebuildIndex/ListTags/Resolve`, `DP.Save.ListSlots/DeleteSlot/DumpHeader`, `DP.UI.DumpStack`.
- **Gameplay Debugger** — a DesignPatterns category overlays FSM state, granted actions and subsystem
  summaries on the selected actor (editor / development builds).
- **Project Settings → Plugins → Design Patterns** — default pools, registry scan paths, save slot,
  command history depth, verbose logging.

---

## 7. The opt-in GAS bridge

`DesignPatternsGAS` is the **only** module that links `GameplayAbilities`, and it is **left out of the
default `.uplugin`** so projects without GAS still compile. To use it:

1. Enable the `GameplayAbilities` plugin in your project.
2. Add `{ "Name": "DesignPatternsGAS", "Type": "Runtime", "LoadingPhase": "Default" }` to the
   `Modules` array in `DesignPatterns.uplugin`.
3. Regenerate project files and build.

---

## 8. Building

> **Requirement:** Unreal Engine **5.3 – 5.5** with the C++ toolchain (Visual Studio 2022 with the
> *Game development with C++* workload on Windows; Xcode on macOS; clang on Linux).

1. Copy the `DesignPatterns` folder into your project's `Plugins/` directory:
   `<YourProject>/Plugins/DesignPatterns/`.
2. Right-click your `.uproject` → **Generate Visual Studio project files**.
3. Open the solution and build **Development Editor** (or build from the editor's *Live Coding* /
   *Compile* button), or from the command line:
   ```
   "<Engine>/Engine/Build/BatchFiles/Build.bat" <YourProject>Editor Win64 Development -Project="<path>/<YourProject>.uproject"
   ```
4. Enable the plugin in **Edit → Plugins → Design Patterns** if it isn't already, and restart.

### Optional plugins
`ModelViewViewModel` and `CommonUI` are referenced as **optional**; the UI module's lite ViewModel
(built on the engine's `FieldNotification`) works without them. `OnlineSubsystem` /
`OnlineSubsystemUtils` are optional for `DesignPatternsNet` — without them, session calls report
"unsupported in this configuration" deterministically (offline-only fallback). With them present,
real sessions are compiled in (gated by `WITH_DP_ONLINE`).

---

## 9. Tests

`DesignPatternsTests` ships Automation tests (Editor → **Tools → Test Automation**, filter
`DesignPatterns.*`), covering message-bus tag routing and listener lifetime, and service-locator
register/resolve/weak-invalidation. They run without a live world where possible, so they are fast
and deterministic.

---

## 10. Static checker (no engine required)

`Tools/dp_static_check.py` mechanically validates the source tree **without** an engine install:
`.generated.h` ordering, one `IMPLEMENT_MODULE` per module, brace/paren balance, `UPROPERTY` raw
pointers, and a zero-tolerance scan for stub / placeholder / TODO markers. Run:

```
python Tools/dp_static_check.py
```

It exits non-zero on any error. This is **not** a substitute for UnrealHeaderTool + the C++ compiler —
always do a real engine build before shipping — but it catches the most common mechanical mistakes
early.
