// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Footstep/Audio_FootstepComponent.h"
#include "Footstep/Audio_SurfaceBankDataAsset.h"
#include "DesignPatternsAudioModule.h"
#include "Seam/Audio_AudioController.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

UAudio_FootstepComponent::UAudio_FootstepComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // event-driven only.
}

void UAudio_FootstepComponent::PlayFootstep(FName FootBone, FGameplayTag Gait)
{
	const FGameplayTag EffectiveGait = Gait.IsValid() ? Gait : CurrentGait;

	UWorld* World = GetWorld();
	if (!World || !SurfaceBank)
	{
		return;
	}

	const FVector FootLoc = GetFootWorldLocation(FootBone);
	const FVector End = FootLoc - FVector(0.f, 0.f, FMath::Max(1.f, TraceDownDistance));

	FCollisionQueryParams Q(SCENE_QUERY_STAT(DP_AudioFootstepComp), /*bTraceComplex=*/true);
	Q.bReturnPhysicalMaterial = true;
	if (const AActor* Owner = GetOwner())
	{
		Q.AddIgnoredActor(Owner);
	}

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, FootLoc, End, TraceChannel.GetValue(), Q))
	{
		return; // Airborne / nothing under foot: no footstep.
	}

	const EPhysicalSurface Surface = UGameplayStatics::GetSurfaceType(Hit);
	PlayResolved(Surface, Hit.ImpactPoint, EffectiveGait);
}

void UAudio_FootstepComponent::PlayFootstepAtHit(const FHitResult& Hit, FGameplayTag Gait)
{
	if (!SurfaceBank)
	{
		return;
	}
	const FGameplayTag EffectiveGait = Gait.IsValid() ? Gait : CurrentGait;
	const EPhysicalSurface Surface = UGameplayStatics::GetSurfaceType(Hit);
	const FVector Loc = Hit.ImpactPoint.IsZero() ? GetFootWorldLocation(NAME_None) : Hit.ImpactPoint;
	PlayResolved(Surface, Loc, EffectiveGait);
}

void UAudio_FootstepComponent::PlayResolved(EPhysicalSurface Surface, const FVector& Location, FGameplayTag Gait)
{
	const FGameplayTag SoundTag = SurfaceBank->ResolveSoundTag(Surface, Gait);
	if (!SoundTag.IsValid())
	{
		UE_LOG(LogDP, VeryVerbose, TEXT("Footstep: no sound mapped for surface %d gait '%s'."),
			static_cast<int32>(Surface), *Gait.ToString());
		return;
	}

	if (IAudio_AudioController* Controller = ResolveAudioController())
	{
		IAudio_AudioController::Execute_PlaySoundAtLocation(Controller->_getUObject(), SoundTag, Location,
			FMath::Max(0.f, FootstepVolumeMult));
	}
}

IAudio_AudioController* UAudio_FootstepComponent::ResolveAudioController() const
{
	UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}
	UObject* Provider = Locator->ResolveService(AudioNativeTags::Service_Audio);
	if (Provider && Provider->GetClass()->ImplementsInterface(UAudio_AudioController::StaticClass()))
	{
		return Cast<IAudio_AudioController>(Provider);
	}
	return nullptr;
}

FVector UAudio_FootstepComponent::GetFootWorldLocation(FName FootBone) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	if (FootBone != NAME_None)
	{
		if (const USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>())
		{
			if (Mesh->DoesSocketExist(FootBone))
			{
				return Mesh->GetSocketLocation(FootBone);
			}
		}
	}
	return Owner->GetActorLocation();
}
