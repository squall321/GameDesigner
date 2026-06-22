// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Reverb/Audio_ReverbZoneVolume.h"
#include "Reverb/Audio_ReverbMixProfileDataAsset.h"
#include "Manager/Audio_SoundManagerSubsystem.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Components/BrushComponent.h"

AAudio_ReverbZoneVolume::AAudio_ReverbZoneVolume()
{
	// A reverb zone is a pure trigger; it must generate overlaps but never block movement.
	if (UBrushComponent* Brush = GetBrushComponent())
	{
		Brush->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Brush->SetCollisionResponseToAllChannels(ECR_Overlap);
		Brush->SetGenerateOverlapEvents(true);
	}
}

void AAudio_ReverbZoneVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Always release a held push so a destroyed / level-unloaded zone cannot leave the reverb stuck on.
	PopReverb();
	OverlapRefCount = 0;
	Super::EndPlay(EndPlayReason);
}

void AAudio_ReverbZoneVolume::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);

	if (!DoesActorQualify(OtherActor))
	{
		return;
	}

	++OverlapRefCount;
	if (OverlapRefCount == 1)
	{
		PushReverb();
	}
}

void AAudio_ReverbZoneVolume::NotifyActorEndOverlap(AActor* OtherActor)
{
	Super::NotifyActorEndOverlap(OtherActor);

	if (!DoesActorQualify(OtherActor))
	{
		return;
	}

	OverlapRefCount = FMath::Max(0, OverlapRefCount - 1);
	if (OverlapRefCount == 0)
	{
		PopReverb();
	}
}

bool AAudio_ReverbZoneVolume::DoesActorQualify(const AActor* Other) const
{
	const APawn* Pawn = Cast<APawn>(Other);
	if (!Pawn)
	{
		return false; // Only pawns drive reverb; ignore projectiles / items / triggers.
	}
	if (!bOnlyLocalListener)
	{
		return true;
	}
	// Local-listener gating: only a locally-controlled pawn (the audio listener on this client) counts,
	// so split-screen / remote pawns never apply this client's reverb.
	const AController* Controller = Pawn->GetController();
	return Controller && Controller->IsLocalController();
}

UAudio_ReverbMixProfileDataAsset* AAudio_ReverbZoneVolume::ResolveProfile() const
{
	if (ReverbProfile)
	{
		return ReverbProfile;
	}
	if (ReverbProfileTag.IsValid())
	{
		if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
		{
			if (UAudio_ReverbMixProfileDataAsset* Resolved = Registry->Find<UAudio_ReverbMixProfileDataAsset>(ReverbProfileTag))
			{
				return Resolved;
			}
			UE_LOG(LogDP, Warning, TEXT("ReverbZone '%s': could not resolve reverb profile tag '%s'."),
				*GetName(), *ReverbProfileTag.ToString());
		}
	}
	return nullptr;
}

void AAudio_ReverbZoneVolume::PushReverb()
{
	if (ActiveHandle.IsValid())
	{
		return; // Already applied.
	}

	UAudio_SoundManagerSubsystem* Manager = FDP_SubsystemStatics::GetGameInstanceSubsystem<UAudio_SoundManagerSubsystem>(this);
	if (!Manager)
	{
		return; // No audio manager (e.g. dedicated server) — nothing to do.
	}

	UAudio_ReverbMixProfileDataAsset* Profile = ResolveProfile();
	if (!Profile)
	{
		return;
	}

	// Reuse the shipped priority-stack/fade machinery via the blended push so the reverb effect fades.
	ActiveHandle = Manager->PushMixProfileAssetBlended(Profile, BlendTimeOverride, ZonePriority);

	UE_LOG(LogDP, Verbose, TEXT("ReverbZone '%s': pushed reverb profile '%s' (priority %d)."),
		*GetName(), *Profile->DataTag.ToString(), ZonePriority);
}

void AAudio_ReverbZoneVolume::PopReverb()
{
	if (!ActiveHandle.IsValid())
	{
		return;
	}
	if (UAudio_SoundManagerSubsystem* Manager = FDP_SubsystemStatics::GetGameInstanceSubsystem<UAudio_SoundManagerSubsystem>(this))
	{
		Manager->PopMixProfile(ActiveHandle);
	}
	ActiveHandle.Invalidate();
}
