// Copyright DesignPatterns plugin. All Rights Reserved.

#include "AntiCheat/UNet_AuthorityGuardComponent.h"
#include "DesignPatternsNetNativeTags.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

UNet_AuthorityGuardComponent::UNet_AuthorityGuardComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UNet_AuthorityGuardComponent::ConsumeRateToken(const FGameplayTag& RequestTag) const
{
	const UWorld* W = GetWorld();
	const double Now = W ? W->GetTimeSeconds() : 0.0;
	const double Window = (double)FMath::Max(0.05f, RateWindowSeconds);

	FRateBucket& Bucket = RateBuckets.FindOrAdd(RequestTag);
	if ((Now - Bucket.WindowStart) >= Window)
	{
		// Window elapsed (>= so a request landing exactly on the boundary starts a fresh window): reset.
		Bucket.WindowStart = Now;
		Bucket.Count = 0;
	}

	if (Bucket.Count >= FMath::Max(1, MaxRequestsPerWindow))
	{
		return false; // over the limit this window
	}
	++Bucket.Count;
	return true;
}

bool UNet_AuthorityGuardComponent::IsOwnerMovementSane() const
{
	if (!bEnforceMovementBounds)
	{
		return true;
	}
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return true;
	}

	// Speed check (uses pawn velocity where available; falls back to actor velocity).
	const FVector Velocity = Owner->GetVelocity();
	if (MaxOwnerSpeed > 0.f && Velocity.SizeSquared() > FMath::Square((double)MaxOwnerSpeed))
	{
		return false;
	}

	// Position bound check (cube centred on origin), if enabled.
	if (WorldBoundHalfExtent > 0.f)
	{
		const FVector Loc = Owner->GetActorLocation();
		const double H = (double)WorldBoundHalfExtent;
		if (FMath::Abs(Loc.X) > H || FMath::Abs(Loc.Y) > H || FMath::Abs(Loc.Z) > H)
		{
			return false;
		}
	}
	return true;
}

void UNet_AuthorityGuardComponent::FlagRejection(const FGameplayTag& RequestTag, const TCHAR* Reason) const
{
	UE_LOG(LogDP, Warning, TEXT("AntiCheat[%s]: rejected request %s (%s)."),
		GetOwner() ? *GetOwner()->GetName() : TEXT("<noowner>"),
		*RequestTag.ToString(), Reason);

	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(NetNativeTags::Bus_Net_AntiCheat_Flagged, FInstancedStruct(), GetOwner());
	}
}

bool UNet_AuthorityGuardComponent::CanServerApplyAction_Implementation(FGameplayTag ActionTag) const
{
	// This runs ON THE SERVER inside the base's apply step. Layer our checks, then defer to Super.
	if (!ConsumeRateToken(ActionTag))
	{
		FlagRejection(ActionTag, TEXT("rate limit exceeded"));
		return false;
	}
	if (!IsOwnerMovementSane())
	{
		FlagRejection(ActionTag, TEXT("movement bounds/speed violation"));
		return false;
	}
	return Super::CanServerApplyAction_Implementation(ActionTag);
}

bool UNet_AuthorityGuardComponent::ValidateServerRequest(FGameplayTag RequestTag)
{
	if (!UNet_NetUtilsLibrary::HasAuthority(GetOwner()))
	{
		return false; // a client never validates server requests
	}
	if (!ConsumeRateToken(RequestTag))
	{
		FlagRejection(RequestTag, TEXT("rate limit exceeded"));
		return false;
	}
	if (!IsOwnerMovementSane())
	{
		FlagRejection(RequestTag, TEXT("movement bounds/speed violation"));
		return false;
	}
	return true;
}
