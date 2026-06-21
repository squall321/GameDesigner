// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Cover/AI_CoverPoint.h"

#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AAI_CoverPoint::AAI_CoverPoint()
{
	bReplicates = true;
	bAlwaysRelevant = true; // cover field is coordination state; keep relevant to every connection
	// A cover point changes only on claim/release; start dormant and wake on mutation.
	NetDormancy = DORM_Initial;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;
}

void AAI_CoverPoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAI_CoverPoint, CoverId);
	DOREPLIFETIME(AAI_CoverPoint, CoverTypeTag);
	DOREPLIFETIME(AAI_CoverPoint, Claimant);
}

void AAI_CoverPoint::BeginPlay()
{
	Super::BeginPlay();
	// A level-placed point seeds its own stable id on the authority right away so claims have a key.
	if (HasAuthority())
	{
		EnsureCoverId();
	}
}

void AAI_CoverPoint::EnsureCoverId()
{
	if (!HasAuthority())
	{
		return;
	}
	if (!CoverId.IsValid())
	{
		CoverId = FSeam_EntityId::NewId();
		WakeForChange();
	}
}

bool AAI_CoverPoint::TryClaim(const FSeam_EntityId& By)
{
	if (!HasAuthority())
	{
		return false;
	}
	if (!By.IsValid())
	{
		return false;
	}
	// Free, or already ours: claim succeeds. Otherwise refuse.
	if (Claimant.IsValid() && Claimant != By)
	{
		return false;
	}
	if (Claimant != By)
	{
		Claimant = By;
		WakeForChange();
		OnCoverClaimChanged.Broadcast(this, Claimant);
	}
	return true;
}

void AAI_CoverPoint::Release(const FSeam_EntityId& By)
{
	if (!HasAuthority())
	{
		return;
	}
	if (Claimant.IsValid() && Claimant == By)
	{
		Claimant = FSeam_EntityId::Invalid();
		WakeForChange();
		OnCoverClaimChanged.Broadcast(this, Claimant);
	}
}

bool AAI_CoverPoint::ProtectsAgainst(const FVector& ThreatWorldDir, float MinDot) const
{
	if (ProtectedDirections.Num() == 0)
	{
		// No authored directions: treat as omnidirectional cover (always protects).
		return true;
	}
	const FVector ThreatDir = ThreatWorldDir.GetSafeNormal();
	if (ThreatDir.IsNearlyZero())
	{
		return true;
	}
	const FQuat Rot = GetActorQuat();
	for (const FVector& LocalDir : ProtectedDirections)
	{
		const FVector WorldDir = Rot.RotateVector(LocalDir).GetSafeNormal();
		if (FVector::DotProduct(WorldDir, ThreatDir) >= MinDot)
		{
			return true;
		}
	}
	return false;
}

void AAI_CoverPoint::OnRep_Claimant(FSeam_EntityId /*PreviousClaimant*/)
{
	OnCoverClaimChanged.Broadcast(this, Claimant);
}

void AAI_CoverPoint::WakeForChange()
{
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
	}
}
