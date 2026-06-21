// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Trace/Move_TraceLibrary.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PhysicsVolume.h"
#include "Engine/World.h"
#include "Core/DPLog.h"

FCollisionQueryParams UMove_TraceLibrary::MakeIgnoreParams(const ACharacter* Character)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(Move_Traversal), /*bTraceComplex=*/false);
	if (Character)
	{
		Params.AddIgnoredActor(Character);
	}
	return Params;
}

FCollisionObjectQueryParams UMove_TraceLibrary::MakeObjectParams(const TArray<TEnumAsByte<ECollisionChannel>>& Channels)
{
	if (Channels.Num() == 0)
	{
		// Documented defensive default: static world geometry is the universal "solid" channel.
		return FCollisionObjectQueryParams(ECC_WorldStatic);
	}
	FCollisionObjectQueryParams Params;
	for (const TEnumAsByte<ECollisionChannel>& Channel : Channels)
	{
		Params.AddObjectTypesToQuery(Channel.GetValue());
	}
	return Params;
}

FMove_LedgeResult UMove_TraceLibrary::FindLedge(const ACharacter* Character, const FMove_LedgeTuning& Tuning)
{
	FMove_LedgeResult Result;

	if (!Character)
	{
		return Result;
	}
	const UWorld* World = Character->GetWorld();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!World || !Capsule)
	{
		return Result;
	}

	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const FVector CapsuleBase = Character->GetActorLocation() - FVector(0.f, 0.f, CapsuleHalfHeight);
	const FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		return Result;
	}

	const FCollisionObjectQueryParams ObjectParams = MakeObjectParams(Tuning.Channels);
	const FCollisionQueryParams QueryParams = MakeIgnoreParams(Character);

	// 1) Forward trace at roughly chest height to detect a wall in front.
	const FVector ForwardStart = CapsuleBase + FVector(0.f, 0.f, CapsuleHalfHeight);
	const FVector ForwardEnd = ForwardStart + Forward * (Tuning.ForwardReach + CapsuleRadius);

	FHitResult WallHit;
	const bool bHitWall = World->LineTraceSingleByObjectType(WallHit, ForwardStart, ForwardEnd, ObjectParams, QueryParams);
	if (!bHitWall)
	{
		return Result;
	}

	// 2) From above the detected wall, trace DOWN to find the ledge top surface.
	const float ProbeUp = Tuning.MaxMantleHeight + CapsuleHalfHeight;
	const FVector OverLedge = WallHit.ImpactPoint - WallHit.ImpactNormal.GetSafeNormal2D() * (CapsuleRadius + 5.f);
	const FVector DownStart = FVector(OverLedge.X, OverLedge.Y, CapsuleBase.Z + ProbeUp);
	const FVector DownEnd = FVector(OverLedge.X, OverLedge.Y, CapsuleBase.Z - 5.f);

	FHitResult TopHit;
	const bool bHitTop = World->LineTraceSingleByObjectType(TopHit, DownStart, DownEnd, ObjectParams, QueryParams);
	if (!bHitTop)
	{
		return Result;
	}

	const float LedgeHeight = TopHit.ImpactPoint.Z - CapsuleBase.Z;
	if (LedgeHeight <= 0.f || LedgeHeight > Tuning.MaxMantleHeight)
	{
		// Too tall to mantle (or below the floor) — not traversable.
		return Result;
	}

	// 3) Confirm there is enough clear standing space on top of the ledge.
	const FVector ClearStart = TopHit.ImpactPoint + FVector(0.f, 0.f, CapsuleHalfHeight * 2.f + 5.f);
	const FVector ClearEnd = ClearStart + Forward * Tuning.RequiredClearDepth;
	FHitResult ClearHit;
	const bool bBlocked = World->SweepSingleByObjectType(
		ClearHit, ClearStart, ClearEnd, FQuat::Identity, ObjectParams,
		FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight), QueryParams);
	if (bBlocked)
	{
		// No room to stand on / land beyond the ledge.
		return Result;
	}

	Result.bFound = true;
	Result.LedgeHeight = LedgeHeight;
	Result.bIsVault = LedgeHeight <= Tuning.VaultMaxHeight;
	Result.LedgeTopLocation = TopHit.ImpactPoint;
	Result.WallNormal = WallHit.ImpactNormal.GetSafeNormal2D();

	// Target: stand on the ledge top, facing into the wall (i.e. opposite the wall normal).
	const FVector TargetLoc = TopHit.ImpactPoint + FVector(0.f, 0.f, CapsuleHalfHeight);
	const FRotator TargetRot = (-Result.WallNormal).Rotation();
	Result.TargetTransform = FTransform(TargetRot, TargetLoc);

	return Result;
}

FMove_WallResult UMove_TraceLibrary::FindWall(const ACharacter* Character, float Distance, bool bRightSide,
	const TArray<TEnumAsByte<ECollisionChannel>>& Channels)
{
	FMove_WallResult Result;
	if (!Character)
	{
		return Result;
	}
	const UWorld* World = Character->GetWorld();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!World || !Capsule)
	{
		return Result;
	}

	const FVector Origin = Character->GetActorLocation();
	const FVector Right = Character->GetActorRightVector().GetSafeNormal2D();
	const FVector Dir = bRightSide ? Right : -Right;
	const FVector End = Origin + Dir * (Distance + Capsule->GetScaledCapsuleRadius());

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByObjectType(
		Hit, Origin, End, MakeObjectParams(Channels), MakeIgnoreParams(Character));
	if (!bHit)
	{
		return Result;
	}

	// Only near-vertical surfaces count as walls (normal nearly horizontal).
	if (FMath::Abs(Hit.ImpactNormal.Z) > 0.4f)
	{
		return Result;
	}

	Result.bFound = true;
	Result.ImpactPoint = Hit.ImpactPoint;
	Result.WallNormal = Hit.ImpactNormal.GetSafeNormal();
	Result.Side = bRightSide ? 1 : -1;
	return Result;
}

bool UMove_TraceLibrary::IsInWater(const ACharacter* Character)
{
	if (!Character)
	{
		return false;
	}
	if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		if (const APhysicsVolume* Volume = Capsule->GetPhysicsVolume())
		{
			return Volume->bWaterVolume;
		}
	}
	return false;
}

float UMove_TraceLibrary::GetFloorSlopeDegrees(const ACharacter* Character)
{
	if (!Character)
	{
		return 0.f;
	}
	if (const UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
	{
		const FFindFloorResult& Floor = CMC->CurrentFloor;
		if (Floor.bBlockingHit)
		{
			// Angle between the floor normal and world-up.
			const float Dot = FMath::Clamp(Floor.HitResult.ImpactNormal.Z, -1.f, 1.f);
			return FMath::RadiansToDegrees(FMath::Acos(Dot));
		}
	}
	return 0.f;
}
