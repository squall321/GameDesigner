// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Shake/Cam_ShakeDirectorComponent.h"
#include "Shake/Cam_ShakeRequestComponent.h"
#include "Cam_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UCam_ShakeDirectorComponent::UCam_ShakeDirectorComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // event-driven; no tick needed.
}

void UCam_ShakeDirectorComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only the locally-controlled player has a camera to shake.
	const APlayerController* PC = ResolveOwningController();
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	RegisterBusRoutes();

	UE_LOG(LogDP, Verbose, TEXT("[Camera] ShakeDirector begun on %s (%d routes)."),
		*GetNameSafe(GetOwner()), ProfileRoutes.Num());
}

void UCam_ShakeDirectorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Remove every bus listener we registered so the bus does not hold us past teardown.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		for (const FDP_ListenerHandle& Handle : ListenerHandles)
		{
			Bus->StopListening(Handle);
		}
	}
	ListenerHandles.Reset();

	Super::EndPlay(EndPlayReason);
}

void UCam_ShakeDirectorComponent::OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options)
{
	// Honour the accessibility "reduce shake" slider for every subsequent shake.
	CachedShakeScale = FMath::Max(Options.ScreenShakeScale, 0.f);
	UE_LOG(LogDP, Verbose, TEXT("[Camera] ShakeDirector cached shake scale=%.2f."), CachedShakeScale);
}

bool UCam_ShakeDirectorComponent::PlayShakeAtLocation(FGameplayTag EventTag, FVector Epicenter, float ExtraScale)
{
	if (bSuppressed || !EventTag.IsValid())
	{
		return false;
	}

	// Find the most specific authored route whose Channel is an ancestor-or-equal of EventTag.
	const FCam_ShakeProfileRoute* BestRoute = nullptr;
	int32 BestDepth = -1;
	for (const FCam_ShakeProfileRoute& Route : ProfileRoutes)
	{
		if (!Route.Channel.IsValid())
		{
			continue;
		}
		if (EventTag == Route.Channel || EventTag.MatchesTag(Route.Channel))
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
		return false;
	}

	UCam_ShakeProfile* Profile = BestRoute->Profile.LoadSynchronous();
	if (!Profile)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Camera] ShakeDirector route '%s' has no profile."), *BestRoute->Channel.ToString());
		return false;
	}

	return PlayProfile(Profile, BestRoute->Channel, Epicenter, ExtraScale * BestRoute->RouteScale);
}

bool UCam_ShakeDirectorComponent::PlayProfile(UCam_ShakeProfile* Profile, FGameplayTag Channel, FVector Epicenter, float ExtraScale)
{
	if (!Profile)
	{
		return false;
	}

	// Distance falloff from the epicenter to the local viewer.
	const FVector Viewer = ResolveViewerLocation();
	const float Distance = FVector::Dist(Viewer, Epicenter);
	const float Falloff = Profile->ComputeScaleAtDistance(Distance);

	const float FinalScale = FMath::Max(Falloff * CachedShakeScale * ExtraScale, 0.f);
	if (FinalScale <= KINDA_SMALL_NUMBER || !Profile->ShakeTag.IsValid())
	{
		return false; // out of range, fully reduced, or no shake row authored.
	}

	bool bStarted = false;

	// Preferred path: delegate to the sibling request component so there is ONE playback path.
	if (UCam_ShakeRequestComponent* Request = ResolveRequestComponent())
	{
		bStarted = Request->PlayShakeByTag(Profile->ShakeTag, FinalScale);
	}
	else if (APlayerCameraManager* Manager = ResolveCameraManager())
	{
		// Fallback: we have no request component but do have a camera manager. We can only honour the
		// scale through a request component's library lookup, so here we cannot resolve a shake class
		// ourselves without coupling — log and treat as a no-op start. Projects should add the sibling
		// request component for advanced shakes (documented dependency).
		UE_LOG(LogDP, Verbose,
			TEXT("[Camera] ShakeDirector has no sibling UCam_ShakeRequestComponent on %s; advanced shake '%s' not played."),
			*GetNameSafe(GetOwner()), *Profile->ShakeTag.ToString());
		bStarted = false;
	}

	if (bStarted && bBroadcastEpicenter)
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FCam_ShakeEpicenterEvent Event;
			Event.Epicenter = Epicenter;
			Event.ShakeTag = Profile->ShakeTag;
			Event.FinalScale = FinalScale;
			Bus->BroadcastPayload(Cam_NativeTags::Bus_ShakeEpicenter,
				FInstancedStruct::Make(Event), GetOwner());
		}
	}

	UE_LOG(LogDP, VeryVerbose, TEXT("[Camera] ShakeDirector played '%s' dist=%.0f falloff=%.2f final=%.2f started=%d"),
		*Channel.ToString(), Distance, Falloff, FinalScale, bStarted);
	return bStarted;
}

void UCam_ShakeDirectorComponent::RegisterBusRoutes()
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	// Deduplicate channels so we subscribe once per distinct channel.
	TSet<FGameplayTag> Seen;
	for (const FCam_ShakeProfileRoute& Route : ProfileRoutes)
	{
		if (!Route.Channel.IsValid() || Seen.Contains(Route.Channel))
		{
			continue;
		}
		Seen.Add(Route.Channel);

		TWeakObjectPtr<UCam_ShakeDirectorComponent> WeakThis(this);
		const FDP_ListenerHandle Handle = Bus->ListenNative(
			Route.Channel,
			[WeakThis](const FDP_Message& Message)
			{
				if (UCam_ShakeDirectorComponent* StrongThis = WeakThis.Get())
				{
					StrongThis->HandleBusMessage(Message);
				}
			},
			this,
			EDP_MessageMatch::ExactOrChild);
		ListenerHandles.Add(Handle);
	}
}

void UCam_ShakeDirectorComponent::HandleBusMessage(const FDP_Message& Message)
{
	if (bSuppressed)
	{
		return;
	}

	// Read an epicenter: prefer the broadcasting instigator's world location, else the viewer location
	// (a global shake centred on the camera). The payload's specific struct varies per producer, so we
	// stay agnostic and use the instigator actor as the spatial origin.
	FVector Epicenter = ResolveViewerLocation();
	if (const UObject* Instigator = Message.Instigator.Get())
	{
		if (const AActor* AsActor = Cast<AActor>(Instigator))
		{
			Epicenter = AsActor->GetActorLocation();
		}
		else if (const UActorComponent* AsComp = Cast<UActorComponent>(Instigator))
		{
			if (const AActor* CompOwner = AsComp->GetOwner())
			{
				Epicenter = CompOwner->GetActorLocation();
			}
		}
	}

	PlayShakeAtLocation(Message.Channel, Epicenter, 1.f);
}

FVector UCam_ShakeDirectorComponent::ResolveViewerLocation() const
{
	if (const APlayerCameraManager* Manager = ResolveCameraManager())
	{
		return Manager->GetCameraLocation();
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorLocation();
	}
	return FVector::ZeroVector;
}

UCam_ShakeRequestComponent* UCam_ShakeDirectorComponent::ResolveRequestComponent() const
{
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<UCam_ShakeRequestComponent>();
	}
	return nullptr;
}

APlayerController* UCam_ShakeDirectorComponent::ResolveOwningController() const
{
	if (const AActor* Owner = GetOwner())
	{
		if (APlayerController* AsPC = const_cast<APlayerController*>(Cast<APlayerController>(Owner)))
		{
			return AsPC;
		}
		if (const APawn* AsPawn = Cast<APawn>(Owner))
		{
			return Cast<APlayerController>(AsPawn->GetController());
		}
	}
	return nullptr;
}

APlayerCameraManager* UCam_ShakeDirectorComponent::ResolveCameraManager() const
{
	const APlayerController* PC = ResolveOwningController();
	return PC ? PC->PlayerCameraManager : nullptr;
}
