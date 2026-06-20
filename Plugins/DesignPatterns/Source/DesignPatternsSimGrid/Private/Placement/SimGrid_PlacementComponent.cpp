// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Placement/SimGrid_PlacementComponent.h"
#include "Placement/SimGrid_PlacementRuleStrategy.h"
#include "Interfaces/SimGrid_Placeable.h"
#include "Interfaces/SimGrid_GhostPreview.h"
#include "SimGrid_DeveloperSettings.h"
#include "SimGrid_NativeTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Identity/Seam_EntityIdentity.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

USimGrid_PlacementComponent::USimGrid_PlacementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// This component holds no replicated state; authoritative state lives on the resolved carriers.
	// It still needs to exist on a replicated, player-owned actor so its Server RPC routes correctly.
	SetIsReplicatedByDefault(false);
}

void USimGrid_PlacementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Drop the cosmetic ghost so it can be GC'd / hidden when the component goes away.
	if (Ghost.GetObject())
	{
		ISimGrid_GhostPreview::Execute_HideGhostPreview(Ghost.GetObject());
	}
	Ghost = nullptr;
	Super::EndPlay(EndPlayReason);
}

bool USimGrid_PlacementComponent::HasOwnerAuthority() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor && OwnerActor->HasAuthority();
}

TScriptInterface<ISeam_TileProviderRead> USimGrid_PlacementComponent::ResolveGrid() const
{
	TScriptInterface<ISeam_TileProviderRead> Out;

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return Out;
	}

	UObject* Provider = Locator->ResolveService(SimGridTags::Service_TileProvider);
	if (Provider && Provider->GetClass()->ImplementsInterface(USeam_TileProviderRead::StaticClass()))
	{
		Out.SetObject(Provider);
		Out.SetInterface(Cast<ISeam_TileProviderRead>(Provider));
	}
	return Out;
}

FGameplayTag USimGrid_PlacementComponent::DeriveOwnerId() const
{
	// Prefer a stable identity from the owning actor's archetype tag if it exposes the identity seam.
	if (const AActor* OwnerActor = GetOwner())
	{
		if (OwnerActor->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
		{
			// Use the archetype tag as the ownership identity. Const_cast only to satisfy the
			// non-const Execute signature; the call is logically const (a read).
			UObject* IdentityObj = const_cast<AActor*>(OwnerActor);
			const FGameplayTag Archetype = ISeam_EntityIdentity::Execute_GetArchetypeTag(IdentityObj);
			if (Archetype.IsValid())
			{
				return Archetype;
			}
		}
	}
	return FGameplayTag();
}

void USimGrid_PlacementComponent::SetGhost(const TScriptInterface<ISimGrid_GhostPreview>& InGhost)
{
	if (Ghost.GetObject() && Ghost.GetObject() != InGhost.GetObject())
	{
		ISimGrid_GhostPreview::Execute_HideGhostPreview(Ghost.GetObject());
	}
	Ghost = InGhost;
}

bool USimGrid_PlacementComponent::BuildContext(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation,
	FSimGrid_PlacementContext& OutContext) const
{
	if (!ActivePlaceable.GetObject())
	{
		return false;
	}

	TScriptInterface<ISeam_TileProviderRead> Grid = ResolveGrid();
	if (!Grid.GetObject())
	{
		return false;
	}

	OutContext.Grid = Grid;
	OutContext.Origin = Origin;
	OutContext.Rotation = Rotation;
	OutContext.OwnerId = DeriveOwnerId();
	OutContext.Footprint = ISimGrid_Placeable::Execute_GetFootprint(ActivePlaceable.GetObject());
	return true;
}

void USimGrid_PlacementComponent::GatherEffectiveRules(const FSimGrid_PlacementContext& /*Context*/,
	TArray<const USimGrid_PlacementRuleStrategy*>& OutRules) const
{
	OutRules.Reset();
	OutRules.Reserve(Rules.Num());
	for (const TObjectPtr<USimGrid_PlacementRuleStrategy>& Rule : Rules)
	{
		if (Rule)
		{
			OutRules.Add(Rule.Get());
		}
	}
	// Note: the placeable's GetRequiredTileTypes are honoured per-footprint-cell by any configured
	// TerrainAllowed rule (designers add one if terrain matters); we do not synthesize a hidden rule
	// here so the rule set stays fully designer-visible and identical on client and server.
}

FSimGrid_PlacementResult USimGrid_PlacementComponent::EvaluateRules(const FSimGrid_PlacementContext& Context,
	const TArray<const USimGrid_PlacementRuleStrategy*>& InRules)
{
	FSimGrid_PlacementResult Result;

	// Resolve the absolute cells once for the caller (UI highlighting, ghost snapping).
	Result.ResolvedCells.Reserve(Context.Footprint.Num());
	for (const FSimGrid_FootprintCell& FC : Context.Footprint)
	{
		Result.ResolvedCells.Add(Context.ResolveCell(FC));
	}

	for (const USimGrid_PlacementRuleStrategy* Rule : InRules)
	{
		if (!Rule)
		{
			continue;
		}
		const FSimGrid_RuleResult RuleResult = Rule->Evaluate(Context);
		Result.Accumulate(RuleResult);
	}
	return Result;
}

FSimGrid_PlacementResult USimGrid_PlacementComponent::ValidatePlacement(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation) const
{
	FSimGrid_PlacementContext Context;
	if (!BuildContext(Origin, Rotation, Context))
	{
		// No grid or no placeable: cannot validate. Report Unknown so callers don't treat it as Valid.
		FSimGrid_PlacementResult Result;
		Result.Validity = ESimGrid_PlacementValidity::Unknown;
		Result.FirstFailureReason = SimGridTags::Fail_StateUnknown;
		return Result;
	}

	// Footprint size cap (defends both UI and the server path).
	const USimGrid_DeveloperSettings* Settings = USimGrid_DeveloperSettings::Get();
	const int32 MaxFootprint = Settings ? FMath::Max(1, Settings->MaxFootprintCells) : 256;
	if (Context.Footprint.Num() > MaxFootprint)
	{
		FSimGrid_PlacementResult Result;
		Result.Validity = ESimGrid_PlacementValidity::Invalid;
		Result.FirstFailureReason = SimGridTags::Fail_OutOfBounds;
		return Result;
	}

	TArray<const USimGrid_PlacementRuleStrategy*> EffectiveRules;
	GatherEffectiveRules(Context, EffectiveRules);
	return EvaluateRules(Context, EffectiveRules);
}

FSimGrid_PlacementResult USimGrid_PlacementComponent::UpdateGhost(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation)
{
	const FSimGrid_PlacementResult Result = ValidatePlacement(Origin, Rotation);
	if (Ghost.GetObject())
	{
		ISimGrid_GhostPreview::Execute_UpdateGhostPreview(Ghost.GetObject(), Origin, Rotation, Result);
	}
	return Result;
}

void USimGrid_PlacementComponent::CancelPlacement()
{
	if (Ghost.GetObject())
	{
		ISimGrid_GhostPreview::Execute_HideGhostPreview(Ghost.GetObject());
	}
}

void USimGrid_PlacementComponent::RequestPlacement(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation)
{
	if (!ActivePlaceable.GetObject())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SimGrid_Placement] RequestPlacement with no ActivePlaceable; ignored."));
		return;
	}

	// Client-side pre-check to avoid obviously-doomed RPCs. We still let Unknown through (the server
	// has the authoritative state), but reject locally-known Invalid placements early.
	const FSimGrid_PlacementResult Local = ValidatePlacement(Origin, Rotation);
	if (Local.Validity == ESimGrid_PlacementValidity::Invalid)
	{
		OnPlacementCommitted.Broadcast(false, Origin, Rotation, Local.FirstFailureReason);
		UE_LOG(LogDP, Verbose, TEXT("[SimGrid_Placement] Local pre-check rejected placement at %s (%s)."),
			*Origin.ToString(), *Local.FirstFailureReason.ToString());
		return;
	}

	if (HasOwnerAuthority())
	{
		// Listen-server / standalone host: commit directly, no RPC needed.
		const FSimGrid_PlacementResult Result = CommitAuthoritative(ActivePlaceable.GetObject(), Origin, Rotation);
		OnPlacementCommitted.Broadcast(Result.IsValid(), Origin, Rotation, Result.FirstFailureReason);
	}
	else
	{
		// Route player intent to the server. The server re-derives and re-checks everything.
		ServerCommitPlacement(ActivePlaceable.GetObject(), Origin, Rotation);
	}
}

bool USimGrid_PlacementComponent::ServerCommitPlacement_Validate(UObject* PlaceableObject, FSeam_CellCoord /*Origin*/, ESimGrid_Rotation /*Rotation*/)
{
	// Reject malformed RPCs at the boundary: the object must implement the placeable seam. We do NOT
	// trust any client-supplied footprint/validity — those are re-derived server-side in the body.
	if (!PlaceableObject)
	{
		return false;
	}
	if (!PlaceableObject->GetClass()->ImplementsInterface(USimGrid_Placeable::StaticClass()))
	{
		return false;
	}
	return true;
}

void USimGrid_PlacementComponent::ServerCommitPlacement_Implementation(UObject* PlaceableObject, FSeam_CellCoord Origin, ESimGrid_Rotation Rotation)
{
	// AUTHORITY GUARD: this is the server entry point; bail if somehow invoked without authority.
	if (!HasOwnerAuthority())
	{
		return;
	}

	const FSimGrid_PlacementResult Result = CommitAuthoritative(PlaceableObject, Origin, Rotation);

	// Surface the outcome on the server (for server-side listeners) and back to the requesting client.
	OnPlacementCommitted.Broadcast(Result.IsValid(), Origin, Rotation, Result.FirstFailureReason);
	ClientNotifyPlacementResult(Result.IsValid(), Origin, Rotation, Result.FirstFailureReason);
}

FSimGrid_PlacementResult USimGrid_PlacementComponent::CommitAuthoritative(UObject* PlaceableObject,
	const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation)
{
	FSimGrid_PlacementResult Result;
	Result.Validity = ESimGrid_PlacementValidity::Invalid;

	// AUTHORITY GUARD.
	if (!HasOwnerAuthority())
	{
		Result.FirstFailureReason = SimGridTags::Fail_StateUnknown;
		return Result;
	}
	if (!PlaceableObject || !PlaceableObject->GetClass()->ImplementsInterface(USimGrid_Placeable::StaticClass()))
	{
		Result.FirstFailureReason = SimGridTags::Fail_StateUnknown;
		return Result;
	}

	TScriptInterface<ISeam_TileProviderRead> Grid = ResolveGrid();
	if (!Grid.GetObject())
	{
		Result.FirstFailureReason = SimGridTags::Fail_StateUnknown;
		return Result;
	}

	// Re-derive the footprint from the placeable AUTHORITATIVELY (never trust the client's copy).
	FSimGrid_PlacementContext Context;
	Context.Grid = Grid;
	Context.Origin = Origin;
	Context.Rotation = Rotation;
	Context.OwnerId = DeriveOwnerId();
	Context.Footprint = ISimGrid_Placeable::Execute_GetFootprint(PlaceableObject);

	// Footprint size cap: reject an oversized footprint a malicious client might submit.
	const USimGrid_DeveloperSettings* Settings = USimGrid_DeveloperSettings::Get();
	const int32 MaxFootprint = Settings ? FMath::Max(1, Settings->MaxFootprintCells) : 256;
	if (Context.Footprint.Num() == 0 || Context.Footprint.Num() > MaxFootprint)
	{
		Result.FirstFailureReason = SimGridTags::Fail_OutOfBounds;
		return Result;
	}

	// Re-run the SAME rule set authoritatively.
	TArray<const USimGrid_PlacementRuleStrategy*> EffectiveRules;
	GatherEffectiveRules(Context, EffectiveRules);
	Result = EvaluateRules(Context, EffectiveRules);

	// On the server every cell is known, so a correctly-authored rule set never returns Unknown here;
	// treat any residual Unknown conservatively as a rejection.
	if (!Result.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SimGrid_Placement] Server rejected placement at %s (%s)."),
			*Origin.ToString(), *Result.FirstFailureReason.ToString());
		return Result;
	}

	// Apply authority-only: drive the placeable's OnPlaced so it claims its cells on the carrier.
	ISimGrid_Placeable::Execute_OnPlaced(PlaceableObject, Origin, Rotation);

	UE_LOG(LogDP, Verbose, TEXT("[SimGrid_Placement] Server committed placement at %s rot=%d (%d cells)."),
		*Origin.ToString(), static_cast<int32>(Rotation), Context.Footprint.Num());
	return Result;
}

void USimGrid_PlacementComponent::ClientNotifyPlacementResult_Implementation(bool bSuccess, FSeam_CellCoord Origin,
	ESimGrid_Rotation Rotation, FGameplayTag Reason)
{
	OnPlacementCommitted.Broadcast(bSuccess, Origin, Rotation, Reason);
}
