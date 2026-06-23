// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Fog/SimGrid_FogRevealComponent.h"
#include "Fog/SimGrid_FogCarrier.h"
#include "Settings/SimGrid_FeatureSettings.h"
#include "Settings/SimGrid_DeveloperSettings.h"
#include "SimGrid_NativeTags.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// ─── Construction / lifecycle ─────────────────────────────────────────────────

USimGrid_FogRevealComponent::USimGrid_FogRevealComponent()
{
    // Replicated so the server is aware of the component (e.g. for RPC routing).
    // The component itself holds no replicated UPROPERTYs — all fog state lives on the carrier.
    SetIsReplicatedByDefault(true);

    // Tick is required to drive the auto-reveal accumulator.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void USimGrid_FogRevealComponent::BeginPlay()
{
    Super::BeginPlay();
    RevealAccumulator = 0.f;
}

void USimGrid_FogRevealComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // No delegates registered; nothing to unbind.
    Super::EndPlay(EndPlayReason);
}

void USimGrid_FogRevealComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                 FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bRevealOnTick)
    {
        return;
    }

    RevealAccumulator += DeltaTime;
    if (RevealAccumulator >= RevealIntervalSeconds)
    {
        RevealAccumulator = 0.f;
        RevealAtOwnerPosition();
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void USimGrid_FogRevealComponent::RevealAtOwnerPosition()
{
    FSeam_CellCoord Cell;
    if (GetOwnerCell(Cell))
    {
        RevealAtCell(Cell);
    }
}

void USimGrid_FogRevealComponent::RevealAtCell(const FSeam_CellCoord& Cell)
{
    const UWorld* W = GetWorld();
    if (!W)
    {
        return;
    }

    // Clamp the radius to the settings cap before sending or applying it.
    const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
    const int32 MaxR        = Features ? Features->GetSafeMaxFogRevealRadius() : 48;
    const int32 SafeRadius  = FMath::Clamp(RevealRadiusCells, 1, MaxR);

    if (W->GetNetMode() == NM_Client)
    {
        // On an owning client: fire the reliable server RPC. The server validates and applies.
        ServerReveal(Cell, SafeRadius);
    }
    else
    {
        // On authority: call the carrier directly without an RPC round-trip.
        ASimGrid_FogCarrier* Carrier = ASimGrid_FogCarrier::ResolveForTeam(this, TeamId);
        if (Carrier)
        {
            Carrier->RevealRadius(Cell, SafeRadius);
        }
        else if (TeamId.IsValid())
        {
            UE_LOG(LogDP, Verbose,
                TEXT("USimGrid_FogRevealComponent::RevealAtCell — no fog carrier found for team '%s'."),
                *TeamId.ToString());
        }
    }
}

// ─── Server RPC ───────────────────────────────────────────────────────────────

bool USimGrid_FogRevealComponent::ServerReveal_Validate(FSeam_CellCoord /*Centre*/, int32 Radius)
{
    // Reject non-positive radius.
    if (Radius <= 0)
    {
        return false;
    }

    // Reject radius exceeding the project cap — prevents a tampered client from rasterising
    // an unbounded number of cells in a single RPC.
    const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
    const int32 MaxR = Features ? Features->GetSafeMaxFogRevealRadius() : 48;
    if (Radius > MaxR)
    {
        UE_LOG(LogDP, Warning,
            TEXT("USimGrid_FogRevealComponent::ServerReveal_Validate — "
                 "radius %d exceeds cap %d; rejecting RPC."),
            Radius, MaxR);
        return false;
    }

    // Reject invalid team (prevents reveal into a non-existent carrier).
    if (!TeamId.IsValid())
    {
        return false;
    }

    return true;
}

void USimGrid_FogRevealComponent::ServerReveal_Implementation(FSeam_CellCoord Centre, int32 Radius)
{
    // AUTHORITY GUARD — the RPC guarantees authority, but check defensively.
    if (!HasAuthority())
    {
        return;
    }

    // Clamp again server-side as a belt-and-suspenders measure.
    const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
    const int32 MaxR       = Features ? Features->GetSafeMaxFogRevealRadius() : 48;
    const int32 SafeRadius = FMath::Clamp(Radius, 1, MaxR);

    ASimGrid_FogCarrier* Carrier = ASimGrid_FogCarrier::ResolveForTeam(this, TeamId);
    if (!Carrier)
    {
        UE_LOG(LogDP, Verbose,
            TEXT("USimGrid_FogRevealComponent::ServerReveal_Implementation — "
                 "no carrier found for team '%s'."),
            *TeamId.ToString());
        return;
    }

    Carrier->RevealRadius(Centre, SafeRadius);
}

// ─── Internal helpers ─────────────────────────────────────────────────────────

bool USimGrid_FogRevealComponent::GetOwnerCell(FSeam_CellCoord& OutCell) const
{
    const AActor* Owner = GetOwner();
    if (!Owner)
    {
        return false;
    }

    // Resolve the tile-provider seam.  Prefer the project-configured tag; fall back to the
    // SimGrid native tag (Service_TileProvider).
    FGameplayTag ProviderKey = SimGridTags::Service_TileProvider;
    if (const USimGrid_DeveloperSettings* Settings = USimGrid_DeveloperSettings::Get())
    {
        if (Settings->TileProviderServiceTag.IsValid())
        {
            ProviderKey = Settings->TileProviderServiceTag;
        }
    }

    // Try the weak cache first to avoid a service-locator lookup every tick.
    UObject* ProviderObj = CachedTileProviderObject.Get();
    if (!ProviderObj || !ProviderObj->Implements<USeam_TileProviderRead>())
    {
        // Cache miss or stale — resolve fresh.
        const UDP_ServiceLocatorSubsystem* Locator =
            FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
        if (!Locator)
        {
            return false;
        }
        ProviderObj = Locator->ResolveService(ProviderKey);
        if (!ProviderObj || !ProviderObj->Implements<USeam_TileProviderRead>())
        {
            UE_LOG(LogDP, Verbose,
                TEXT("USimGrid_FogRevealComponent::GetOwnerCell — "
                     "tile provider not available under tag '%s'."),
                *ProviderKey.ToString());
            return false;
        }
        CachedTileProviderObject = ProviderObj;
    }

    OutCell = ISeam_TileProviderRead::Execute_WorldToCell(ProviderObj, Owner->GetActorLocation());
    return true;
}
