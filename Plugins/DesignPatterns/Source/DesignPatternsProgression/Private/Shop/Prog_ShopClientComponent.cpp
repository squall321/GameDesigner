// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Shop/Prog_ShopClientComponent.h"
#include "Shop/Prog_ShopComponent.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

UProg_ShopClientComponent::UProg_ShopClientComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// This component holds no replicated state; it only routes a server RPC. It still needs to be
	// replicated so its owning connection is established for the Server/Client RPCs to route.
	SetIsReplicatedByDefault(true);
}

AActor* UProg_ShopClientComponent::ResolveBuyerActor() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	// The buyer is the actor whose wallet/inventory pays/receives. This component may live on the
	// PlayerController, PlayerState or pawn; resolve to the controlled pawn where possible so the range
	// check has a world position, falling back to the owner itself (e.g. a menu shop with no pawn).
	if (const APawn* AsPawn = Cast<APawn>(Owner))
	{
		return const_cast<APawn*>(AsPawn);
	}
	if (const AController* AsController = Cast<AController>(Owner))
	{
		if (APawn* Pawn = AsController->GetPawn())
		{
			return Pawn;
		}
		return const_cast<AController*>(AsController);
	}
	if (const APlayerState* AsPlayerState = Cast<APlayerState>(Owner))
	{
		if (APawn* Pawn = AsPlayerState->GetPawn())
		{
			return Pawn;
		}
	}
	return Owner;
}

UProg_ShopComponent* UProg_ShopClientComponent::ResolveShop(const AActor* Vendor)
{
	if (!Vendor)
	{
		return nullptr;
	}
	return const_cast<AActor*>(Vendor)->FindComponentByClass<UProg_ShopComponent>();
}

bool UProg_ShopClientComponent::IsWithinInteractionRange(const AActor* Buyer, const AActor* Vendor) const
{
	// Range check disabled, or we have nothing to compare against -> allow (documented fallback).
	if (MaxInteractionDistance <= 0.f || !Buyer || !Vendor)
	{
		return true;
	}

	const float DistSq = FVector::DistSquared(Buyer->GetActorLocation(), Vendor->GetActorLocation());
	const bool bInRange = DistSq <= (MaxInteractionDistance * MaxInteractionDistance);
	if (!bInRange)
	{
		UE_LOG(LogDP, Verbose,
			TEXT("[Prog_ShopClient] Purchase rejected: buyer %s is %.0fcm from vendor %s (max %.0f)."),
			*GetNameSafe(Buyer), FMath::Sqrt(DistSq), *GetNameSafe(Vendor), MaxInteractionDistance);
	}
	return bInRange;
}

void UProg_ShopClientComponent::RequestPurchase(AActor* Vendor, int32 EntryIndex)
{
	if (!Vendor || EntryIndex < 0)
	{
		UE_LOG(LogDP, Warning, TEXT("[Prog_ShopClient] RequestPurchase ignored: invalid vendor/index."));
		return;
	}

	// Forward the intent to the server. On a listen-server host this still routes through the RPC path,
	// executing the same authoritative flow (HasAuthority is true there).
	Server_Purchase(Vendor, EntryIndex);
}

bool UProg_ShopClientComponent::Server_Purchase_Validate(AActor* Vendor, int32 EntryIndex)
{
	// Cheap structural validation only; the body does the authoritative re-checks. Reject obviously
	// malformed requests so a misbehaving client cannot spam the expensive path with garbage.
	return Vendor != nullptr && EntryIndex >= 0;
}

void UProg_ShopClientComponent::Server_Purchase_Implementation(AActor* Vendor, int32 EntryIndex)
{
	// AUTHORITY: this body only ever runs on the server. Re-derive everything; trust nothing.
	AActor* Buyer = ResolveBuyerActor();
	EProg_PurchaseResult Result = EProg_PurchaseResult::NoShop;

	if (!Buyer)
	{
		UE_LOG(LogDP, Warning, TEXT("[Prog_ShopClient] Server_Purchase: could not resolve buyer for %s."),
			*GetNameSafe(GetOwner()));
		Client_PurchaseResult(Vendor, EntryIndex, EProg_PurchaseResult::NoPurchaseTarget);
		return;
	}

	UProg_ShopComponent* Shop = ResolveShop(Vendor);
	if (!Shop)
	{
		Client_PurchaseResult(Vendor, EntryIndex, EProg_PurchaseResult::NoShop);
		return;
	}

	// Server-side interaction-range gate (anti-cheat: client cannot buy from across the map).
	if (!IsWithinInteractionRange(Buyer, Vendor))
	{
		Client_PurchaseResult(Vendor, EntryIndex, EProg_PurchaseResult::BadEntry);
		return;
	}

	// Delegate to the vendor's authoritative purchase flow, which re-validates unlock/stock/affordability.
	Result = Shop->TryPurchase(Buyer, EntryIndex);

	// Deliver the decision to the owning connection's UI. The Client RPC routes to whichever connection
	// owns this component: on a dedicated server that is the remote client; on a listen-server host that
	// is the host itself (where it executes locally exactly once). Either way OnPurchaseResult fires
	// once, on the right machine — so we deliberately do NOT broadcast directly here.
	Client_PurchaseResult(Vendor, EntryIndex, Result);
}

void UProg_ShopClientComponent::Client_PurchaseResult_Implementation(AActor* Vendor, int32 EntryIndex, EProg_PurchaseResult Result)
{
	// Runs on the owning client; hand the server's decision to bound UI.
	OnPurchaseResult.Broadcast(Vendor, EntryIndex, Result);
}
