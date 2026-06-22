// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Footstep/Audio_FootstepLibrary.h"
#include "Footstep/Audio_SurfaceBankDataAsset.h"
#include "DesignPatternsAudioModule.h"
#include "Seam/Audio_AudioController.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "CollisionQueryParams.h"

EPhysicalSurface UAudio_FootstepLibrary::GetSurfaceFromHit(const FHitResult& Hit)
{
	// WRAP the engine: UGameplayStatics::GetSurfaceType reads the hit's physical material surface.
	return UGameplayStatics::GetSurfaceType(Hit);
}

void UAudio_FootstepLibrary::PlaySurfaceFootstep(UObject* WorldContextObject, FVector Location,
	UAudio_SurfaceBankDataAsset* Bank, FGameplayTag Gait, float VolumeMult)
{
	if (!WorldContextObject || !Bank)
	{
		return;
	}

	// Resolve which surface to use is the caller's job (it passed the bank); we map gait here. The
	// surface itself is encoded into the bank lookup by the caller via PlayFootstepTraceDown; this
	// entry point assumes SurfaceType_Default unless the caller pre-resolved. Use the fallback path.
	const FGameplayTag SoundTag = Bank->ResolveSoundTag(SurfaceType_Default, Gait);
	if (!SoundTag.IsValid())
	{
		return;
	}

	UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContextObject);
	if (!Locator)
	{
		return;
	}
	UObject* Provider = Locator->ResolveService(AudioNativeTags::Service_Audio);
	if (!Provider || !Provider->GetClass()->ImplementsInterface(UAudio_AudioController::StaticClass()))
	{
		return;
	}

	IAudio_AudioController::Execute_PlaySoundAtLocation(Provider, SoundTag, Location, VolumeMult);
}

bool UAudio_FootstepLibrary::PlayFootstepTraceDown(UObject* WorldContextObject, FVector Origin, float TraceDownDistance,
	TEnumAsByte<ECollisionChannel> TraceChannel, UAudio_SurfaceBankDataAsset* Bank, FGameplayTag Gait, float VolumeMult)
{
	if (!WorldContextObject || !Bank)
	{
		return false;
	}
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return false;
	}

	const FVector End = Origin - FVector(0.f, 0.f, FMath::Max(1.f, TraceDownDistance));

	FCollisionQueryParams Q(SCENE_QUERY_STAT(DP_AudioFootstep), /*bTraceComplex=*/true);
	Q.bReturnPhysicalMaterial = true; // required for GetSurfaceType to resolve a surface.

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Origin, End, TraceChannel.GetValue(), Q))
	{
		return false;
	}

	const EPhysicalSurface Surface = UGameplayStatics::GetSurfaceType(Hit);
	const FGameplayTag SoundTag = Bank->ResolveSoundTag(Surface, Gait);
	if (!SoundTag.IsValid())
	{
		return false;
	}

	UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContextObject);
	if (!Locator)
	{
		return false;
	}
	UObject* Provider = Locator->ResolveService(AudioNativeTags::Service_Audio);
	if (!Provider || !Provider->GetClass()->ImplementsInterface(UAudio_AudioController::StaticClass()))
	{
		return false;
	}

	IAudio_AudioController::Execute_PlaySoundAtLocation(Provider, SoundTag, Hit.ImpactPoint, VolumeMult);
	return true;
}
