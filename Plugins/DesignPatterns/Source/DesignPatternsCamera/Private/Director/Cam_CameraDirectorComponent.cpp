// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Director/Cam_CameraDirectorComponent.h"
#include "Director/Cam_CameraModeStack.h"
#include "Director/Cam_CameraModifier.h"
#include "Mode/Cam_StandardModes.h"
#include "Mode/Cam_CinematicMode.h"
#include "Collision/Cam_CameraCollisionProbe.h"
#include "PostProcess/Cam_PostProcessModifier.h"
#include "PostProcess/Cam_PostProcessProfile.h"
#include "Settings/Cam_DeveloperSettings.h"
#include "Cam_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "Input/Seam_InputModeArbiter.h"
#include "Identity/Seam_EntityId.h"
#include "Identity/Seam_EntityIdentity.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UCam_CameraDirectorComponent::UCam_CameraDirectorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Tick late so the pawn has finished moving and the control rotation is up to date before we frame it.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bWantsInitializeComponent = false;
}

void UCam_CameraDirectorComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only the locally-controlled player needs a camera director; remote proxies have no local camera.
	const APlayerController* PC = ResolveOwningController();
	if (!PC || !PC->IsLocalController())
	{
		// Disable ticking on non-local instances but keep the component (it may gain local control later
		// on listen servers; we re-check in TickComponent's guard as well).
		SetComponentTickEnabled(false);
	}

	// Build the stack (instanced subobject, GC-rooted via the ModeStack UPROPERTY).
	ModeStack = NewObject<UCam_CameraModeStack>(this, UCam_CameraModeStack::StaticClass(), TEXT("CameraModeStack"));

	// Wire the blend curve from settings (defensive: settings CDO is always present, curve may be null).
	if (const UCam_DeveloperSettings* Settings = UCam_DeveloperSettings::Get())
	{
		if (UCurveFloat* Curve = Settings->BlendCurve.LoadSynchronous())
		{
			ModeStack->SetBlendCurve(Curve);
		}

		// Push the configured default mode at its base priority so the camera always has behaviour.
		if (Settings->DefaultModeTag.IsValid())
		{
			DefaultModeRequestId = PushCameraMode_Implementation(Settings->DefaultModeTag, Settings->DefaultModePriority);
		}

		if (Settings->bRegisterAsService)
		{
			RegisterAsProviderService();
		}
	}

	UE_LOG(LogDP, Log, TEXT("[Camera] Director begun on %s (local=%d)."),
		*GetNameSafe(GetOwner()), PC ? PC->IsLocalController() : 0);
}

void UCam_CameraDirectorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Release any held input-mode request.
	if (LookCaptureInputRequestId.IsValid())
	{
		if (TScriptInterface<ISeam_InputModeArbiter> Arbiter = ResolveInputArbiter())
		{
			ISeam_InputModeArbiter::Execute_PopInputMode(Arbiter.GetObject(), LookCaptureInputRequestId);
		}
		LookCaptureInputRequestId.Invalidate();
	}

	UnregisterProviderService();

	// Remove our modifier(s) from the manager so a respawned director can re-add cleanly.
	if (APlayerCameraManager* Manager = ResolveCameraManager())
	{
		if (UCam_CameraModifier* Mod = Modifier.Get())
		{
			Manager->RemoveCameraModifier(Mod);
		}
		if (UCam_PostProcessModifier* PPMod = PostProcessModifier.Get())
		{
			Manager->RemoveCameraModifier(PPMod);
		}
	}
	Modifier.Reset();
	PostProcessModifier.Reset();

	if (ModeStack)
	{
		ModeStack->ClearAll();
	}

	Super::EndPlay(EndPlayReason);
}

void UCam_CameraDirectorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APlayerController* PC = ResolveOwningController();
	if (!PC || !PC->IsLocalController() || !ModeStack)
	{
		return; // not the local player: nothing to drive.
	}

	UCam_CameraModifier* Mod = EnsureModifier();
	if (!Mod)
	{
		return; // no camera manager yet (e.g. very early frame); try again next tick.
	}

	const FCam_ViewContext Context = BuildViewContext();

	// Fallback = the live camera POV, so an empty or mid-blend stack eases from the real camera.
	FCam_CameraView Fallback;
	if (const APlayerCameraManager* Manager = ResolveCameraManager())
	{
		Fallback.Location = Manager->GetCameraLocation();
		Fallback.Rotation = Manager->GetCameraRotation();
		Fallback.FOV = Manager->GetFOVAngle();
	}
	else if (bHasLastView)
	{
		Fallback = LastAppliedView;
	}

	if (ModeStack->IsEmpty())
	{
		// No modes: let the manager's own POV stand. Clear post-process too so we contribute nothing.
		Mod->ClearDesiredView();
		if (UCam_PostProcessModifier* PPMod = PostProcessModifier.Get())
		{
			PPMod->ClearDesiredPostProcess();
		}
		LastAppliedView = Fallback;
		bHasLastView = true;
		UpdateInputModeForTopMode();
		return;
	}

	FCam_CameraView Blended = ModeStack->EvaluateStack(DeltaTime, Context, Fallback);

	// Post-evaluation collision/occlusion stage: pull the camera off geometry and (optionally) broadcast
	// a cosmetic occlusion alpha. Runs AFTER the blend and never touches APlayerCameraManager.
	Blended = RunCollisionStage(Context, Blended, DeltaTime);

	Mod->SetDesiredView(Blended);

	// Post-process stage: resolve the active mode's DOF/vignette/grain preset and feed the 2nd modifier.
	RunPostProcessStage();

	LastAppliedView = Blended;
	bHasLastView = true;

	UpdateInputModeForTopMode();
}

FCam_CameraView UCam_CameraDirectorComponent::RunCollisionStage(const FCam_ViewContext& Context, const FCam_CameraView& Blended, float DeltaTime)
{
	if (!CollisionProbe)
	{
		return Blended;
	}

	FCam_CameraView Adjusted;
	FCam_CollisionResult Result;
	CollisionProbe->ResolveCollision(this, Context, Blended, DeltaTime, Adjusted, Result);

	if (bBroadcastOcclusion && Result.bTargetOccluded)
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FCam_OcclusionEvent Event;
			Event.OcclusionAlpha = Result.OcclusionAlpha;
			// Best-effort: read the view target's stable id if it exposes the identity seam.
			if (const AActor* TargetActor = Context.ViewTarget.Get())
			{
				if (TargetActor->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
				{
					Event.TargetId = ISeam_EntityIdentity::Execute_GetEntityId(const_cast<AActor*>(TargetActor));
				}
			}
			Bus->BroadcastPayload(Cam_NativeTags::Bus_CameraOcclusion, FInstancedStruct::Make(Event), GetOwner());
		}
	}

	return Adjusted;
}

void UCam_CameraDirectorComponent::RunPostProcessStage()
{
	UCam_PostProcessProfile* Profile = ResolvePostProcessProfile();
	if (!Profile)
	{
		return; // no profile authored: leave the post-process pipeline untouched.
	}

	UCam_PostProcessModifier* PPMod = EnsurePostProcessModifier();
	if (!PPMod)
	{
		return;
	}

	FCam_PostProcessSettings Settings;
	if (Profile->ResolveForMode(GetActiveModeTag_Implementation(), Settings))
	{
		PPMod->SetDesiredPostProcess(Settings);
	}
	else
	{
		PPMod->ClearDesiredPostProcess();
	}
}

UCam_PostProcessProfile* UCam_CameraDirectorComponent::ResolvePostProcessProfile()
{
	if (bPostProcessProfileResolved)
	{
		return CachedPostProcessProfile;
	}
	bPostProcessProfileResolved = true;
	if (!PostProcessProfileOverride.IsNull())
	{
		CachedPostProcessProfile = PostProcessProfileOverride.LoadSynchronous();
	}
	return CachedPostProcessProfile;
}

UCam_PostProcessModifier* UCam_CameraDirectorComponent::EnsurePostProcessModifier()
{
	if (UCam_PostProcessModifier* Existing = PostProcessModifier.Get())
	{
		return Existing;
	}
	APlayerCameraManager* Manager = ResolveCameraManager();
	if (!Manager)
	{
		return nullptr;
	}
	UCameraModifier* New = Manager->AddNewCameraModifier(UCam_PostProcessModifier::StaticClass());
	UCam_PostProcessModifier* Typed = Cast<UCam_PostProcessModifier>(New);
	if (Typed)
	{
		PostProcessModifier = Typed;
		UE_LOG(LogDP, Verbose, TEXT("[Camera] Installed post-process modifier on %s."), *GetNameSafe(Manager));
	}
	return Typed;
}

//~ ISeam_CinematicCameraSink ------------------------------------------------------------------

FGuid UCam_CameraDirectorComponent::BeginCinematicOverride_Implementation(FTransform POV, float FOV, float BlendInTime)
{
	if (!ModeStack)
	{
		return FGuid();
	}

	// Build a transient override mode owned by the stack; seed its blend times and initial POV.
	UCam_CinematicOverrideMode* Mode = NewObject<UCam_CinematicOverrideMode>(ModeStack, UCam_CinematicOverrideMode::StaticClass());
	if (!Mode)
	{
		return FGuid();
	}
	Mode->SetModeTag(Cam_NativeTags::Mode_CinematicOverride);
	Mode->ConfigureBlend(FMath::Max(BlendInTime, 0.f), /*default out*/ 0.5f);
	Mode->SetOverride(POV, FOV);

	const FGuid RequestId = ModeStack->PushMode(Mode, CinematicOverridePriority, BuildViewContext());
	UE_LOG(LogDP, Log, TEXT("[Camera] Begin cinematic override on %s (blendIn=%.2f)."), *GetNameSafe(GetOwner()), BlendInTime);
	return RequestId;
}

void UCam_CameraDirectorComponent::UpdateCinematicOverride_Implementation(FGuid Handle, FTransform POV, float FOV)
{
	if (!ModeStack || !Handle.IsValid())
	{
		return;
	}
	// Reach the override mode: it is at the cinematic priority. We locate it by checking the top mode
	// (overrides are highest priority); if it is the override, drive it.
	if (UCam_CinematicOverrideMode* Override = Cast<UCam_CinematicOverrideMode>(ModeStack->GetTopMode()))
	{
		Override->SetOverride(POV, FOV);
	}
}

void UCam_CameraDirectorComponent::EndCinematicOverride_Implementation(FGuid Handle, float BlendOutTime)
{
	if (!ModeStack || !Handle.IsValid())
	{
		return;
	}
	// Seed the desired blend-out on the mode before popping so the stack eases its weight out.
	if (UCam_CinematicOverrideMode* Override = Cast<UCam_CinematicOverrideMode>(ModeStack->GetTopMode()))
	{
		Override->ConfigureBlend(Override->GetBlendInTime(), FMath::Max(BlendOutTime, 0.f));
	}
	ModeStack->PopMode(Handle);
	UE_LOG(LogDP, Log, TEXT("[Camera] End cinematic override on %s (blendOut=%.2f)."), *GetNameSafe(GetOwner()), BlendOutTime);
}

//~ ICam_CameraModeProvider --------------------------------------------------------------------

FGuid UCam_CameraDirectorComponent::PushCameraMode_Implementation(FGameplayTag ModeTag, int32 Priority)
{
	if (!ModeStack)
	{
		UE_LOG(LogDP, Warning, TEXT("[Camera] PushCameraMode before stack init on %s."), *GetNameSafe(GetOwner()));
		return FGuid();
	}
	if (!ModeTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[Camera] PushCameraMode with invalid tag on %s."), *GetNameSafe(GetOwner()));
		return FGuid();
	}

	UCam_CameraMode* Mode = InstanceModeForTag(ModeTag);
	if (!Mode)
	{
		return FGuid(); // InstanceModeForTag already logged.
	}

	const FGuid RequestId = ModeStack->PushMode(Mode, Priority, BuildViewContext());
	UpdateInputModeForTopMode();
	return RequestId;
}

void UCam_CameraDirectorComponent::PopCameraMode_Implementation(FGuid RequestId)
{
	if (ModeStack && RequestId.IsValid())
	{
		ModeStack->PopMode(RequestId);
		UpdateInputModeForTopMode();
	}
}

FGameplayTag UCam_CameraDirectorComponent::GetActiveModeTag_Implementation() const
{
	return ModeStack ? ModeStack->GetTopModeTag() : FGameplayTag();
}

//~ Internals ----------------------------------------------------------------------------------

APlayerController* UCam_CameraDirectorComponent::ResolveOwningController() const
{
	if (APlayerController* Cached = CachedController.Get())
	{
		return Cached;
	}

	APlayerController* Found = nullptr;
	if (const AActor* Owner = GetOwner())
	{
		// Owner may be the controller itself, or a pawn whose controller is a PlayerController.
		if (APlayerController* AsPC = const_cast<APlayerController*>(Cast<APlayerController>(Owner)))
		{
			Found = AsPC;
		}
		else if (const APawn* AsPawn = Cast<APawn>(Owner))
		{
			Found = Cast<APlayerController>(AsPawn->GetController());
		}
	}

	CachedController = Found;
	return Found;
}

APlayerCameraManager* UCam_CameraDirectorComponent::ResolveCameraManager() const
{
	const APlayerController* PC = ResolveOwningController();
	return PC ? PC->PlayerCameraManager : nullptr;
}

UCam_CameraModifier* UCam_CameraDirectorComponent::EnsureModifier()
{
	if (UCam_CameraModifier* Existing = Modifier.Get())
	{
		return Existing;
	}

	APlayerCameraManager* Manager = ResolveCameraManager();
	if (!Manager)
	{
		return nullptr;
	}

	// AddNewCameraModifier instantiates and registers the modifier with the manager (manager owns it).
	UCameraModifier* New = Manager->AddNewCameraModifier(UCam_CameraModifier::StaticClass());
	UCam_CameraModifier* Typed = Cast<UCam_CameraModifier>(New);
	if (Typed)
	{
		Modifier = Typed;
		UE_LOG(LogDP, Verbose, TEXT("[Camera] Installed director modifier on %s."), *GetNameSafe(Manager));
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("[Camera] Failed to install director modifier on %s."), *GetNameSafe(Manager));
	}
	return Typed;
}

FCam_ViewContext UCam_CameraDirectorComponent::BuildViewContext() const
{
	FCam_ViewContext Context;

	const APlayerController* PC = ResolveOwningController();
	AActor* Target = nullptr;
	if (PC)
	{
		Target = PC->GetViewTarget();
		Context.ControlRotation = PC->GetControlRotation();
	}
	if (!Target)
	{
		Target = GetOwner();
	}
	Context.ViewTarget = Target;

	// Pivot: socket on the first skeletal mesh if configured, else actor location + offset.
	FVector Pivot = FVector::ZeroVector;
	if (Target)
	{
		Pivot = Target->GetActorLocation();
		if (PivotSocketName != NAME_None)
		{
			if (const USkeletalMeshComponent* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>())
			{
				if (Mesh->DoesSocketExist(PivotSocketName))
				{
					Pivot = Mesh->GetSocketLocation(PivotSocketName);
				}
				else
				{
					Pivot += Target->GetActorTransform().TransformVector(PivotOffset);
				}
			}
			else
			{
				Pivot += Target->GetActorTransform().TransformVector(PivotOffset);
			}
		}
		else
		{
			Pivot += Target->GetActorTransform().TransformVector(PivotOffset);
		}
	}
	Context.PivotLocation = Pivot;
	Context.WorldUp = FVector::UpVector;

	if (bHasLastView)
	{
		Context.PreviousCameraLocation = LastAppliedView.Location;
		Context.PreviousCameraRotation = LastAppliedView.Rotation;
	}
	else if (const APlayerCameraManager* Manager = ResolveCameraManager())
	{
		Context.PreviousCameraLocation = Manager->GetCameraLocation();
		Context.PreviousCameraRotation = Manager->GetCameraRotation();
	}

	return Context;
}

UCam_CameraMode* UCam_CameraDirectorComponent::InstanceModeForTag(FGameplayTag ModeTag)
{
	const UCam_DeveloperSettings* Settings = UCam_DeveloperSettings::Get();
	if (!Settings)
	{
		UE_LOG(LogDP, Warning, TEXT("[Camera] No camera developer settings CDO; cannot resolve mode %s."), *ModeTag.ToString());
		return nullptr;
	}

	const TSubclassOf<UCam_CameraMode> ModeClass = Settings->ResolveModeClass(ModeTag);
	if (!*ModeClass)
	{
		return nullptr; // ResolveModeClass logged.
	}

	// Instance with the stack as Outer so it is owned by/serialized with the stack.
	UObject* Outer = ModeStack ? static_cast<UObject*>(ModeStack) : static_cast<UObject*>(this);
	UCam_CameraMode* Mode = NewObject<UCam_CameraMode>(Outer, ModeClass);
	if (Mode)
	{
		Mode->SetModeTag(ModeTag);
	}
	return Mode;
}

void UCam_CameraDirectorComponent::RegisterAsProviderService()
{
	if (bRegisteredService)
	{
		return;
	}
	UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}
	// WeakObserved: the locator must not keep a dead pawn's director alive across respawn/level travel.
	const bool bOk = Locator->RegisterService(Cam_NativeTags::Service_ModeProvider, this,
		EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	bRegisteredService = bOk;

	// Also publish the cinematic-sink seam under its own key so cutscenes can blend the local camera
	// without knowing this concrete type. Same WeakObserved lifetime so we never leak across travel.
	Locator->RegisterService(Cam_NativeTags::Service_CinematicSink, this,
		EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);

	UE_LOG(LogDP, Verbose, TEXT("[Camera] Registered mode-provider + cinematic-sink services (ok=%d)."), bOk);
}

void UCam_CameraDirectorComponent::UnregisterProviderService()
{
	if (!bRegisteredService)
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// Only unregister if we are still the bound provider (avoid clobbering a newer local player's director).
		if (Locator->ResolveService(Cam_NativeTags::Service_ModeProvider) == this)
		{
			Locator->UnregisterService(Cam_NativeTags::Service_ModeProvider);
		}
		if (Locator->ResolveService(Cam_NativeTags::Service_CinematicSink) == this)
		{
			Locator->UnregisterService(Cam_NativeTags::Service_CinematicSink);
		}
	}
	bRegisteredService = false;
}

TScriptInterface<ISeam_InputModeArbiter> UCam_CameraDirectorComponent::ResolveInputArbiter() const
{
	// The input arbiter is registered by the Platform module under a stable service tag. We resolve it
	// generically: the shared seam header is included, but we do not know the Platform module's key tag
	// here, so we scan by interface on the resolved provider object would be ideal; instead the platform
	// registers it under the input-mode-arbiter seam's conventional service key. We resolve via the
	// service locator using that conventional key string anchored under DP.Service.
	UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(const_cast<UCam_CameraDirectorComponent*>(this));
	if (!Locator)
	{
		return nullptr;
	}
	// Conventional key under which the Platform input router publishes the arbiter seam.
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

void UCam_CameraDirectorComponent::UpdateInputModeForTopMode()
{
	// Decide whether the current top mode wants the look-capture input mode.
	bool bWantCapture = false;
	if (ModeStack)
	{
		if (const UCam_OrbitMode* Orbit = Cast<UCam_OrbitMode>(ModeStack->GetTopMode()))
		{
			bWantCapture = Orbit->WantsLookCaptureInput();
		}
	}

	const bool bHolding = LookCaptureInputRequestId.IsValid();
	if (bWantCapture == bHolding)
	{
		return; // already in the desired state.
	}

	TScriptInterface<ISeam_InputModeArbiter> Arbiter = ResolveInputArbiter();
	if (!Arbiter)
	{
		return; // no arbiter available; the mode still works, it just doesn't capture input exclusively.
	}

	if (bWantCapture)
	{
		LookCaptureInputRequestId = ISeam_InputModeArbiter::Execute_PushInputMode(
			Arbiter.GetObject(), Cam_NativeTags::InputMode, LookCaptureInputPriority);
	}
	else
	{
		ISeam_InputModeArbiter::Execute_PopInputMode(Arbiter.GetObject(), LookCaptureInputRequestId);
		LookCaptureInputRequestId.Invalidate();
	}
}
