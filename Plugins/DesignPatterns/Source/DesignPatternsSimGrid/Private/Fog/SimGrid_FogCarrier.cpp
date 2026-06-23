// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Fog/SimGrid_FogCarrier.h"
#include "Settings/SimGrid_FeatureSettings.h"
#include "SimGrid_NativeTags.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// ─── Fast-array item callbacks (invoked on CLIENTS only) ─────────────────────

void FSimGrid_FogRun::PreReplicatedRemove(const FSimGrid_FogRunArray& InArraySerializer)
{
    if (InArraySerializer.OwnerCarrier)
    {
        // Notify for the span's anchor cell; the renderer invalidates the full span
        // because it has access to the full run data before removal.
        InArraySerializer.OwnerCarrier->HandleReplicatedFogChange(FSeam_CellCoord(StartX, RowY));
    }
}

void FSimGrid_FogRun::PostReplicatedAdd(const FSimGrid_FogRunArray& InArraySerializer)
{
    if (InArraySerializer.OwnerCarrier)
    {
        InArraySerializer.OwnerCarrier->HandleReplicatedFogChange(FSeam_CellCoord(StartX, RowY));
    }
}

void FSimGrid_FogRun::PostReplicatedChange(const FSimGrid_FogRunArray& InArraySerializer)
{
    if (InArraySerializer.OwnerCarrier)
    {
        InArraySerializer.OwnerCarrier->HandleReplicatedFogChange(FSeam_CellCoord(StartX, RowY));
    }
}

// ─── Construction / lifecycle ─────────────────────────────────────────────────

ASimGrid_FogCarrier::ASimGrid_FogCarrier()
{
    bReplicates = true;
    // Each carrier belongs to one team. bOnlyRelevantToOwner=true means only the connection
    // whose controller owns this actor receives its replicated data. The spawner must call
    // SetOwner() to the appropriate team controller before or immediately after spawning.
    bOnlyRelevantToOwner = true;
    bAlwaysRelevant = false;
    SetReplicatingMovement(false);
    NetUpdateFrequency = 10.f;
    // Start dormant so an idle carrier costs no per-frame bandwidth; flush dormancy whenever
    // fog state changes so the delta reaches clients this frame.
    NetDormancy = DORM_Initial;
    PrimaryActorTick.bCanEverTick = false;

    // Wire back-pointers here so they are valid if any early callback fires.
    ExploredRuns.OwnerCarrier = this;
    VisibleRuns.OwnerCarrier  = this;
}

void ASimGrid_FogCarrier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ASimGrid_FogCarrier, TeamId);
    DOREPLIFETIME(ASimGrid_FogCarrier, ExploredRuns);
    DOREPLIFETIME(ASimGrid_FogCarrier, VisibleRuns);
}

void ASimGrid_FogCarrier::BeginPlay()
{
    Super::BeginPlay();

    // Re-wire back-pointers after replication initialisation in case they were reset.
    ExploredRuns.OwnerCarrier = this;
    VisibleRuns.OwnerCarrier  = this;

    RegisterService();
}

void ASimGrid_FogCarrier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnregisterService();
    Super::EndPlay(EndPlayReason);
}

// ─── Service registration ─────────────────────────────────────────────────────

void ASimGrid_FogCarrier::RegisterService()
{
    if (const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get())
    {
        RegisteredServiceTag = Features->FogCarrierServiceTag;
    }
    if (!RegisteredServiceTag.IsValid())
    {
        RegisteredServiceTag = SimGridTags::Service_FogCarrier;
    }

    if (UDP_ServiceLocatorSubsystem* Locator =
        FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
    {
        // WeakObserved: the GI-scoped locator must not extend a world actor's lifetime
        // past its owning world.  bAllowOverride=true so the last-spawned carrier wins
        // in single-team worlds (the typical case).
        Locator->RegisterService(RegisteredServiceTag, this,
                                 EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
    }
    else
    {
        UE_LOG(LogDP, Warning,
            TEXT("ASimGrid_FogCarrier::RegisterService — no UDP_ServiceLocatorSubsystem found."));
    }
}

void ASimGrid_FogCarrier::UnregisterService()
{
    if (RegisteredServiceTag.IsValid())
    {
        if (UDP_ServiceLocatorSubsystem* Locator =
            FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
        {
            Locator->UnregisterService(RegisteredServiceTag);
        }
    }
}

// ─── Static resolvers ─────────────────────────────────────────────────────────

ASimGrid_FogCarrier* ASimGrid_FogCarrier::Resolve(const UObject* WorldContextObject)
{
    FGameplayTag Key = SimGridTags::Service_FogCarrier;
    if (const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get())
    {
        if (Features->FogCarrierServiceTag.IsValid())
        {
            Key = Features->FogCarrierServiceTag;
        }
    }

    if (const UDP_ServiceLocatorSubsystem* Locator =
        FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContextObject))
    {
        return Cast<ASimGrid_FogCarrier>(Locator->ResolveService(Key));
    }
    return nullptr;
}

ASimGrid_FogCarrier* ASimGrid_FogCarrier::ResolveForTeam(const UObject* WorldContextObject,
                                                          const FSeam_EntityId& TeamId)
{
    if (!TeamId.IsValid())
    {
        UE_LOG(LogDP, Warning,
            TEXT("ASimGrid_FogCarrier::ResolveForTeam — invalid TeamId supplied."));
        return nullptr;
    }

    if (!WorldContextObject)
    {
        return nullptr;
    }

    const UWorld* World = WorldContextObject->GetWorld();
    if (!World)
    {
        return nullptr;
    }

    // O(carriers) linear search — there is one carrier per team, so this is acceptable.
    for (TActorIterator<ASimGrid_FogCarrier> It(World); It; ++It)
    {
        ASimGrid_FogCarrier* Carrier = *It;
        if (IsValid(Carrier) && Carrier->TeamId == TeamId)
        {
            return Carrier;
        }
    }

    UE_LOG(LogDP, Verbose,
        TEXT("ASimGrid_FogCarrier::ResolveForTeam — no carrier found for team '%s'."),
        *TeamId.ToString());
    return nullptr;
}

// ─── Internal helpers ─────────────────────────────────────────────────────────

void ASimGrid_FogCarrier::WakeForChange()
{
    FlushNetDormancy();
}

bool ASimGrid_FogCarrier::RunArrayContains(const FSimGrid_FogRunArray& RunArray,
                                            const FSeam_CellCoord& Cell)
{
    for (const FSimGrid_FogRun& Run : RunArray.Entries)
    {
        if (Run.RowY == Cell.Y && Run.StartX <= Cell.X && Run.EndX >= Cell.X)
        {
            return true;
        }
    }
    return false;
}

bool ASimGrid_FogCarrier::EnsureCellInRunArray(FSimGrid_FogRunArray& RunArray,
                                                const FSeam_CellCoord& Cell,
                                                bool bCurrentlyVisible,
                                                ASimGrid_FogCarrier* /*Carrier*/)
{
    // If already covered by any run, this is a no-op (explored cells are permanent).
    if (RunArrayContains(RunArray, Cell))
    {
        return false;
    }

    // Insert a new single-cell run.  We intentionally do NOT merge adjacent runs to keep
    // dirty-delta granularity fine: merging would dirty neighbouring runs on every reveal
    // boundary, wasting more bandwidth than a few extra one-cell entries.
    FSimGrid_FogRun NewRun;
    NewRun.RowY             = Cell.Y;
    NewRun.StartX           = Cell.X;
    NewRun.EndX             = Cell.X;
    NewRun.bCurrentlyVisible = bCurrentlyVisible;

    FSimGrid_FogRun& Added = RunArray.Entries.Add_GetRef(NewRun);
    RunArray.MarkItemDirty(Added);
    return true;
}

bool ASimGrid_FogCarrier::RemoveCellFromRunArray(FSimGrid_FogRunArray& RunArray,
                                                  const FSeam_CellCoord& Cell,
                                                  ASimGrid_FogCarrier* /*Carrier*/)
{
    // Remove any run that overlaps Cell.  Because single-cell runs are always written
    // by EnsureCellInRunArray, a run that contains Cell will either be exactly one cell
    // wide (and can be removed whole) or span multiple cells (uncommon; remove the whole
    // run rather than split — ConcealAll is the preferred batch path).
    bool bRemovedAny = false;
    for (int32 i = RunArray.Entries.Num() - 1; i >= 0; --i)
    {
        const FSimGrid_FogRun& Run = RunArray.Entries[i];
        if (Run.RowY == Cell.Y && Run.StartX <= Cell.X && Run.EndX >= Cell.X)
        {
            RunArray.Entries.RemoveAt(i);
            bRemovedAny = true;
        }
    }
    if (bRemovedAny)
    {
        RunArray.MarkArrayDirty();
    }
    return bRemovedAny;
}

// ─── Authority mutators ───────────────────────────────────────────────────────

void ASimGrid_FogCarrier::RevealRadius(const FSeam_CellCoord& Centre, int32 RadiusCells)
{
    // AUTHORITY GUARD — must be first.
    if (!HasAuthority())
    {
        return;
    }

    const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
    const int32 MaxR         = Features ? Features->GetSafeMaxFogRevealRadius() : 48;
    const int32 ClampedR     = FMath::Clamp(RadiusCells, 1, MaxR);
    const bool bTrackVisible = Features ? Features->bTrackCurrentVisibility : true;

    bool bAnyChanged = false;

    for (int32 DY = -ClampedR; DY <= ClampedR; ++DY)
    {
        for (int32 DX = -ClampedR; DX <= ClampedR; ++DX)
        {
            // Square (Chebyshev) neighbourhood maps cleanly to grid axes.
            const FSeam_CellCoord Cell(Centre.X + DX, Centre.Y + DY);

            // Explore (permanent).
            const bool bExploredNew = EnsureCellInRunArray(ExploredRuns, Cell,
                                                            /*bCurrentlyVisible*/ false,
                                                            this);

            // Visible (ephemeral) — only when setting is active.
            bool bVisibleNew = false;
            if (bTrackVisible)
            {
                bVisibleNew = EnsureCellInRunArray(VisibleRuns, Cell,
                                                   /*bCurrentlyVisible*/ true,
                                                   this);
            }

            if (bExploredNew || bVisibleNew)
            {
                bAnyChanged = true;
                OnFogChanged.Broadcast(this, Cell);
            }
        }
    }

    if (bAnyChanged)
    {
        WakeForChange();
    }
}

void ASimGrid_FogCarrier::ConcealRadius(const FSeam_CellCoord& Centre, int32 RadiusCells)
{
    // AUTHORITY GUARD — must be first.
    if (!HasAuthority())
    {
        return;
    }

    const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
    const bool bTrackVisible = Features ? Features->bTrackCurrentVisibility : true;

    // Nothing to conceal if visible tracking is disabled.
    if (!bTrackVisible)
    {
        return;
    }

    const int32 MaxR     = Features ? Features->GetSafeMaxFogRevealRadius() : 48;
    const int32 ClampedR = FMath::Clamp(RadiusCells, 1, MaxR);

    bool bAnyChanged = false;

    for (int32 DY = -ClampedR; DY <= ClampedR; ++DY)
    {
        for (int32 DX = -ClampedR; DX <= ClampedR; ++DX)
        {
            const FSeam_CellCoord Cell(Centre.X + DX, Centre.Y + DY);
            if (RemoveCellFromRunArray(VisibleRuns, Cell, this))
            {
                bAnyChanged = true;
                OnFogChanged.Broadcast(this, Cell);
            }
        }
    }

    if (bAnyChanged)
    {
        WakeForChange();
    }
}

void ASimGrid_FogCarrier::ConcealAll()
{
    // AUTHORITY GUARD — must be first.
    if (!HasAuthority())
    {
        return;
    }

    if (VisibleRuns.Entries.Num() == 0)
    {
        return; // Nothing to conceal.
    }

    // Collect anchor cells BEFORE clearing so we can broadcast per-run notifications.
    TArray<FSeam_CellCoord> WasVisible;
    WasVisible.Reserve(VisibleRuns.Entries.Num());
    for (const FSimGrid_FogRun& Run : VisibleRuns.Entries)
    {
        WasVisible.Add(FSeam_CellCoord(Run.StartX, Run.RowY));
    }

    VisibleRuns.Entries.Reset();
    VisibleRuns.MarkArrayDirty();
    WakeForChange();

    for (const FSeam_CellCoord& Cell : WasVisible)
    {
        OnFogChanged.Broadcast(this, Cell);
    }
}

// ─── Read queries (client-safe) ───────────────────────────────────────────────

bool ASimGrid_FogCarrier::IsExplored(const FSeam_CellCoord& Cell) const
{
    return RunArrayContains(ExploredRuns, Cell);
}

bool ASimGrid_FogCarrier::IsCurrentlyVisible(const FSeam_CellCoord& Cell) const
{
    const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
    if (!Features || !Features->bTrackCurrentVisibility)
    {
        return false;
    }
    return RunArrayContains(VisibleRuns, Cell);
}

// ─── Notification hook ────────────────────────────────────────────────────────

void ASimGrid_FogCarrier::HandleReplicatedFogChange(const FSeam_CellCoord& Cell)
{
    OnFogChanged.Broadcast(this, Cell);
}
