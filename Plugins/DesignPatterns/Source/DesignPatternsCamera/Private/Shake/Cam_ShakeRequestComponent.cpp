// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Shake/Cam_ShakeRequestComponent.h"
#include "Shake/Cam_CameraShakeLibrary.h"
#include "Settings/Cam_DeveloperSettings.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraShakeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UCam_ShakeRequestComponent::UCam_ShakeRequestComponent()
{
	// Purely reactive: no tick needed. Shakes are driven by bus messages and explicit calls.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Cosmetic local-only component; never replicated.
	SetIsReplicatedByDefault(false);
}

void UCam_ShakeRequestComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveActiveLibrary();
	RegisterBusRoutes();
}

void UCam_ShakeRequestComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unhook every bus listener owned by this component so no dangling handler survives us.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UCam_ShakeRequestComponent::ResolveActiveLibrary()
{
	// 1) Explicit per-component override wins.
	if (!ShakeLibraryOverride.IsNull())
	{
		ActiveLibrary = ShakeLibraryOverride.LoadSynchronous();
		if (ActiveLibrary)
		{
			return;
		}
		UE_LOG(LogDP, Warning, TEXT("[Cam_Shake] %s: ShakeLibraryOverride failed to load; falling back to settings."),
			*GetNameSafe(GetOwner()));
	}

	// 2) Project default from settings CDO (defensive: CDO is never null in a running game).
	if (const UCam_DeveloperSettings* Settings = UCam_DeveloperSettings::Get())
	{
		if (!Settings->DefaultShakeLibrary.IsNull())
		{
			ActiveLibrary = Settings->DefaultShakeLibrary.LoadSynchronous();
		}
	}

	if (!ActiveLibrary)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Cam_Shake] %s: no shake library resolved (override + settings empty); shakes disabled."),
			*GetNameSafe(GetOwner()));
	}
}

void UCam_ShakeRequestComponent::RegisterBusRoutes()
{
	if (BusRoutes.Num() == 0)
	{
		return;
	}

	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		UE_LOG(LogDP, Warning, TEXT("[Cam_Shake] %s: message bus subsystem unavailable; bus-driven shakes inactive."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// Subscribe once per distinct channel; a single handler routes by the broadcast channel.
	TSet<FGameplayTag> SubscribedChannels;
	for (const FCam_ShakeBusRoute& Route : BusRoutes)
	{
		if (!Route.Channel.IsValid() || SubscribedChannels.Contains(Route.Channel))
		{
			continue;
		}
		SubscribedChannels.Add(Route.Channel);

		// Owner-lifetime is this component: when GC'd the listener auto-prunes; we also explicitly
		// StopListeningForOwner in EndPlay. ExactOrChild so a route on DP.Bus.Combat.Damaged also
		// catches DP.Bus.Combat.Damaged.Critical.
		Bus->ListenNative(
			Route.Channel,
			[WeakThis = TWeakObjectPtr<UCam_ShakeRequestComponent>(this)](const FDP_Message& Message)
			{
				if (UCam_ShakeRequestComponent* StrongThis = WeakThis.Get())
				{
					StrongThis->HandleBusMessage(Message);
				}
			},
			this,
			EDP_MessageMatch::ExactOrChild);
	}
}

void UCam_ShakeRequestComponent::HandleBusMessage(const FDP_Message& Message)
{
	if (!bShakesEnabled)
	{
		return;
	}

	// Find the most specific authored route whose Channel is an ancestor-or-equal of the broadcast.
	const FCam_ShakeBusRoute* BestRoute = nullptr;
	int32 BestDepth = -1;
	for (const FCam_ShakeBusRoute& Route : BusRoutes)
	{
		if (!Route.Channel.IsValid())
		{
			continue;
		}
		if (Message.Channel == Route.Channel || Message.Channel.MatchesTag(Route.Channel))
		{
			const int32 Depth = Route.Channel.ToString().Len();
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				BestRoute = &Route;
			}
		}
	}

	if (!BestRoute)
	{
		return;
	}

	// Route's explicit ShakeTag, else fall back to the broadcast channel as the library key.
	const FGameplayTag ShakeKey = BestRoute->ShakeTag.IsValid() ? BestRoute->ShakeTag : Message.Channel;
	PlayShakeByTag(ShakeKey, BestRoute->RouteScale);
}

bool UCam_ShakeRequestComponent::PlayShakeByTag(FGameplayTag ShakeTag, float RequestScale)
{
	if (!bShakesEnabled)
	{
		return false;
	}
	if (!ShakeTag.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("[Cam_Shake] PlayShakeByTag called with invalid tag."));
		return false;
	}
	if (!ActiveLibrary)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Cam_Shake] %s: no active library; cannot play '%s'."),
			*GetNameSafe(GetOwner()), *ShakeTag.ToString());
		return false;
	}

	const FCam_ShakeEntry* Entry = ActiveLibrary->FindEntryExact(ShakeTag);
	if (!Entry)
	{
		// Allow channel-style lookup as a fallback so library rows keyed by bus channel still resolve.
		Entry = ActiveLibrary->FindEntryForChannel(ShakeTag);
	}
	if (!Entry || Entry->ShakeClass == nullptr)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Cam_Shake] %s: no shake class mapped for '%s'."),
			*GetNameSafe(GetOwner()), *ShakeTag.ToString());
		return false;
	}

	APlayerCameraManager* CameraManager = ResolveCameraManager();
	if (!CameraManager)
	{
		// Expected on dedicated servers / non-local actors — Verbose, not a warning.
		UE_LOG(LogDP, Verbose, TEXT("[Cam_Shake] %s: no local camera manager; '%s' not played."),
			*GetNameSafe(GetOwner()), *ShakeTag.ToString());
		return false;
	}

	const float EffectiveScale = FMath::Max(0.f, Entry->DefaultScale)
		* FMath::Max(0.f, RequestScale)
		* FMath::Max(0.f, GlobalShakeScale);
	if (EffectiveScale <= 0.f)
	{
		return false;
	}

	// Engine wrap: let APlayerCameraManager own the shake instance, blending and lifetime.
	UCameraShakeBase* Instance = CameraManager->StartCameraShake(Entry->ShakeClass, EffectiveScale);
	UE_LOG(LogDP, Verbose, TEXT("[Cam_Shake] %s: played '%s' (class %s) scale %.3f -> %s."),
		*GetNameSafe(GetOwner()), *ShakeTag.ToString(), *GetNameSafe(Entry->ShakeClass),
		EffectiveScale, Instance ? TEXT("ok") : TEXT("null"));
	return Instance != nullptr;
}

void UCam_ShakeRequestComponent::StopShakes(TSubclassOf<UCameraShakeBase> ShakeClass, bool bImmediately)
{
	APlayerCameraManager* CameraManager = ResolveCameraManager();
	if (!CameraManager)
	{
		return;
	}

	if (ShakeClass)
	{
		CameraManager->StopAllInstancesOfCameraShake(ShakeClass, bImmediately);
	}
	else
	{
		CameraManager->StopAllCameraShakes(bImmediately);
	}
}

void UCam_ShakeRequestComponent::SetShakeLibrary(UCam_CameraShakeLibrary* InLibrary)
{
	ActiveLibrary = InLibrary;
}

UCam_CameraShakeLibrary* UCam_ShakeRequestComponent::GetActiveShakeLibrary() const
{
	return ActiveLibrary;
}

APlayerController* UCam_ShakeRequestComponent::ResolveOwningPlayerController() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	// Owner may BE a player controller, or be a pawn possessed by one.
	if (APlayerController* AsPC = const_cast<APlayerController*>(Cast<APlayerController>(OwnerActor)))
	{
		return AsPC;
	}
	if (const APawn* AsPawn = Cast<APawn>(OwnerActor))
	{
		return Cast<APlayerController>(AsPawn->GetController());
	}
	return nullptr;
}

APlayerCameraManager* UCam_ShakeRequestComponent::ResolveCameraManager() const
{
	APlayerController* PC = ResolveOwningPlayerController();
	// Only locally-controlled players have a meaningful camera manager for cosmetic shake.
	if (!PC || !PC->IsLocalController())
	{
		return nullptr;
	}
	return PC->PlayerCameraManager;
}
