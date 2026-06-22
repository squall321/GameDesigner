// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Photo/Cam_PhotoModeComponent.h"
#include "Seam/Cam_CameraModeProvider.h"
#include "Director/Cam_CameraDirectorComponent.h"
#include "Director/Cam_CameraModeStack.h"
#include "Shake/Cam_ShakeRequestComponent.h"
#include "Shake/Cam_ShakeDirectorComponent.h"
#include "Cam_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Input/Seam_InputModeArbiter.h"
#include "Display/Seam_SafeZoneProvider.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UCam_PhotoModeComponent::UCam_PhotoModeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics; // feed input before the director evaluates the stack.
}

void UCam_PhotoModeComponent::BeginPlay()
{
	Super::BeginPlay();

	const APlayerController* PC = ResolveOwningController();
	if (!PC || !PC->IsLocalController())
	{
		SetComponentTickEnabled(false);
	}
	else
	{
		SetComponentTickEnabled(false); // only tick while photo mode is active.
	}
}

void UCam_PhotoModeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Ensure we never leave a pushed mode / input mode / shake suppression dangling on teardown.
	if (bActive)
	{
		ExitPhotoMode();
	}
	Super::EndPlay(EndPlayReason);
}

void UCam_PhotoModeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bActive)
	{
		FeedInputToLiveMode();
	}
}

void UCam_PhotoModeComponent::EnterPhotoMode()
{
	if (bActive)
	{
		return;
	}

	const APlayerController* PC = ResolveOwningController();
	if (!PC || !PC->IsLocalController())
	{
		return; // photo mode is local-only.
	}

	TScriptInterface<ICam_CameraModeProvider> Provider = ResolveModeProvider();
	if (!Provider || !Provider.GetObject())
	{
		UE_LOG(LogDP, Warning, TEXT("[Camera] PhotoMode could not resolve a mode provider on %s."), *GetNameSafe(GetOwner()));
		return;
	}

	// Push the free-fly photo camera mode at high priority so it wins over gameplay modes.
	ModeRequestId = ICam_CameraModeProvider::Execute_PushCameraMode(
		Provider.GetObject(), Cam_NativeTags::Mode_PhotoFreeFly, PhotoModePriority);
	if (!ModeRequestId.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[Camera] PhotoMode failed to push Cam.Mode.PhotoFreeFly (is it mapped in settings?)."));
		return;
	}

	// Push the photo input mode through the shared arbiter (Platform owns the engine input config).
	if (TScriptInterface<ISeam_InputModeArbiter> Arbiter = ResolveInputArbiter())
	{
		InputRequestId = ISeam_InputModeArbiter::Execute_PushInputMode(
			Arbiter.GetObject(), Cam_NativeTags::InputMode_PhotoMode, PhotoInputPriority);
	}

	if (bSuppressShakes)
	{
		SetShakesSuppressed(true);
	}

	Accumulated.Reset();
	bActive = true;
	SetComponentTickEnabled(true);
	OnPhotoModeChanged.Broadcast(true);

	UE_LOG(LogDP, Log, TEXT("[Camera] PhotoMode entered on %s."), *GetNameSafe(GetOwner()));
}

void UCam_PhotoModeComponent::ExitPhotoMode()
{
	if (!bActive)
	{
		return;
	}

	// Pop the camera mode by id.
	if (ModeRequestId.IsValid())
	{
		if (TScriptInterface<ICam_CameraModeProvider> Provider = ResolveModeProvider())
		{
			ICam_CameraModeProvider::Execute_PopCameraMode(Provider.GetObject(), ModeRequestId);
		}
		ModeRequestId.Invalidate();
	}

	// Pop the input mode by id.
	if (InputRequestId.IsValid())
	{
		if (TScriptInterface<ISeam_InputModeArbiter> Arbiter = ResolveInputArbiter())
		{
			ISeam_InputModeArbiter::Execute_PopInputMode(Arbiter.GetObject(), InputRequestId);
		}
		InputRequestId.Invalidate();
	}

	if (bSuppressShakes)
	{
		SetShakesSuppressed(false);
	}

	bActive = false;
	SetComponentTickEnabled(false);
	OnPhotoModeChanged.Broadcast(false);

	UE_LOG(LogDP, Log, TEXT("[Camera] PhotoMode exited on %s."), *GetNameSafe(GetOwner()));
}

void UCam_PhotoModeComponent::TogglePhotoMode()
{
	bActive ? ExitPhotoMode() : EnterPhotoMode();
}

void UCam_PhotoModeComponent::AddMoveInput(FVector LocalDelta)
{
	if (bActive)
	{
		Accumulated.MoveDelta += LocalDelta;
	}
}

void UCam_PhotoModeComponent::AddLookInput(FRotator LookDelta)
{
	if (bActive)
	{
		Accumulated.LookDelta += LookDelta;
	}
}

void UCam_PhotoModeComponent::AddRollInput(float RollAxis)
{
	if (bActive)
	{
		Accumulated.RollDelta += RollAxis;
	}
}

void UCam_PhotoModeComponent::AddFOVInput(float FOVDelta)
{
	if (bActive)
	{
		Accumulated.FOVDelta += FOVDelta;
	}
}

FVector4 UCam_PhotoModeComponent::GetSafeInsets() const
{
	if (TScriptInterface<ISeam_SafeZoneProvider> SafeZone = ResolveSafeZoneProvider())
	{
		return ISeam_SafeZoneProvider::Execute_GetSafeInsets(SafeZone.GetObject());
	}
	return FVector4(0.f, 0.f, 0.f, 0.f);
}

//~ Internals ----------------------------------------------------------------------------------

void UCam_PhotoModeComponent::FeedInputToLiveMode()
{
	TScriptInterface<ICam_CameraModeProvider> Provider = ResolveModeProvider();
	if (!Provider || !Provider.GetObject())
	{
		return;
	}

	// Reach the live photo mode through the director's public stack accessor. The mode is guaranteed
	// top at the photo priority while active; if something else is on top we simply skip this frame.
	if (UCam_CameraDirectorComponent* Director = Cast<UCam_CameraDirectorComponent>(Provider.GetObject()))
	{
		if (UCam_CameraModeStack* Stack = Director->GetStack())
		{
			if (UCam_PhotoFreeFlyMode* PhotoMode = Cast<UCam_PhotoFreeFlyMode>(Stack->GetTopMode()))
			{
				PhotoMode->ApplyInput(Accumulated);
			}
		}
	}

	Accumulated.Reset();
}

void UCam_PhotoModeComponent::SetShakesSuppressed(bool bSuppressed)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (UCam_ShakeRequestComponent* Request = Owner->FindComponentByClass<UCam_ShakeRequestComponent>())
	{
		// The request component exposes bShakesEnabled via its own API; we toggle through SetShakeLibrary
		// would be wrong, so we use the dedicated suppression on the advanced director instead and only
		// stop in-flight shakes here.
		Request->StopShakes(nullptr, /*bImmediately=*/true);
	}
	if (UCam_ShakeDirectorComponent* ShakeDir = Owner->FindComponentByClass<UCam_ShakeDirectorComponent>())
	{
		ShakeDir->SetSuppressed(bSuppressed);
	}
}

TScriptInterface<ICam_CameraModeProvider> UCam_PhotoModeComponent::ResolveModeProvider() const
{
	if (ModeProviderCache.GetObject() && ModeProviderCache.GetInterface())
	{
		return ModeProviderCache;
	}

	UCam_PhotoModeComponent* MutableThis = const_cast<UCam_PhotoModeComponent*>(this);

	// Prefer the director on the owner (or its controller).
	const AActor* Owner = GetOwner();
	UObject* Found = nullptr;
	if (Owner)
	{
		if (UActorComponent* Comp = Owner->FindComponentByInterface(UCam_CameraModeProvider::StaticClass()))
		{
			Found = Comp;
		}
		else if (const APawn* AsPawn = Cast<APawn>(Owner))
		{
			if (AActor* Controller = AsPawn->GetController())
			{
				if (UActorComponent* CtrlComp = Controller->FindComponentByInterface(UCam_CameraModeProvider::StaticClass()))
				{
					Found = CtrlComp;
				}
			}
		}
	}

	// Fall back to the service locator.
	if (!Found)
	{
		if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(MutableThis))
		{
			UObject* Provider = Locator->ResolveService(Cam_NativeTags::Service_ModeProvider);
			if (Provider && Provider->GetClass()->ImplementsInterface(UCam_CameraModeProvider::StaticClass()))
			{
				Found = Provider;
			}
		}
	}

	if (Found)
	{
		TScriptInterface<ICam_CameraModeProvider> Result;
		Result.SetObject(Found);
		Result.SetInterface(Cast<ICam_CameraModeProvider>(Found));
		MutableThis->ModeProviderCache = Result;
		return Result;
	}
	return nullptr;
}

TScriptInterface<ISeam_InputModeArbiter> UCam_PhotoModeComponent::ResolveInputArbiter() const
{
	UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(
		const_cast<UCam_PhotoModeComponent*>(this));
	if (!Locator)
	{
		return nullptr;
	}
	static const FGameplayTag ArbiterKey = FGameplayTag::RequestGameplayTag(FName("DP.Service.Input.ModeArbiter"), /*ErrorIfNotFound=*/false);
	if (!ArbiterKey.IsValid())
	{
		return nullptr;
	}
	UObject* Provider = Locator->ResolveService(ArbiterKey);
	if (Provider && Provider->GetClass()->ImplementsInterface(USeam_InputModeArbiter::StaticClass()))
	{
		TScriptInterface<ISeam_InputModeArbiter> Result;
		Result.SetObject(Provider);
		Result.SetInterface(Cast<ISeam_InputModeArbiter>(Provider));
		return Result;
	}
	return nullptr;
}

TScriptInterface<ISeam_SafeZoneProvider> UCam_PhotoModeComponent::ResolveSafeZoneProvider() const
{
	UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(
		const_cast<UCam_PhotoModeComponent*>(this));
	if (!Locator)
	{
		return nullptr;
	}
	static const FGameplayTag SafeZoneKey = FGameplayTag::RequestGameplayTag(FName("DP.Service.Platform.SafeZone"), /*ErrorIfNotFound=*/false);
	if (!SafeZoneKey.IsValid())
	{
		return nullptr;
	}
	UObject* Provider = Locator->ResolveService(SafeZoneKey);
	if (Provider && Provider->GetClass()->ImplementsInterface(USeam_SafeZoneProvider::StaticClass()))
	{
		TScriptInterface<ISeam_SafeZoneProvider> Result;
		Result.SetObject(Provider);
		Result.SetInterface(Cast<ISeam_SafeZoneProvider>(Provider));
		return Result;
	}
	return nullptr;
}

APlayerController* UCam_PhotoModeComponent::ResolveOwningController() const
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
