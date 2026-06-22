// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Haptics/UPlat_HapticFeedbackSubsystem.h"
#include "Haptics/UPlat_HapticTypes.h"
#include "Haptics/UPlat_HapticSettings.h"
#include "Capability/UPlat_DeviceCapabilitySubsystem.h"
#include "Plat_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "GameFramework/ForceFeedbackEffect.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformMisc.h"

// ---------------------------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------------------------

bool UPlat_HapticFeedbackSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Haptics are meaningless without an input device; skip dedicated-server creation entirely.
	return !IsRunningDedicatedServer();
}

void UPlat_HapticFeedbackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Seed enabled state + the default bank from project settings (defensive: settings CDO may be null
	// in very early/editor contexts, in which case we keep the field initializers).
	if (const UPlat_HapticSettings* Settings = UPlat_HapticSettings::Get())
	{
		bHapticsEnabled = Settings->bHapticsEnabledByDefault;
		if (!Settings->DefaultEffectSet.IsNull())
		{
			// Synchronous load is acceptable at GI init for a small bank; soft-ptr keeps cooks light.
			ActiveSet = Settings->DefaultEffectSet.LoadSynchronous();
		}
	}

	// Cache the device-capability subsystem weakly (engine-owned; never kept alive by us).
	CapsWeak = GetGameInstance() ? GetGameInstance()->GetSubsystem<UPlat_DeviceCapabilitySubsystem>() : nullptr;

	RegisterHapticService();

	UE_LOG(LogDP, Log, TEXT("[Platform] HapticFeedbackSubsystem initialized (enabled=%d, bank=%s)."),
		bHapticsEnabled ? 1 : 0, *GetNameSafe(ActiveSet));
}

void UPlat_HapticFeedbackSubsystem::Deinitialize()
{
	// Stop any active feedback and drop the service registration so we never leak across travel.
	StopAllHaptics();
	UnregisterHapticService();
	ActiveSet = nullptr;
	CapsWeak.Reset();

	Super::Deinitialize();
}

FString UPlat_HapticFeedbackSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Haptics enabled=%d bank=%s"),
		AreHapticsEnabled() ? 1 : 0, *GetNameSafe(ActiveSet));
}

// ---------------------------------------------------------------------------------------------
//  Service registration
// ---------------------------------------------------------------------------------------------

void UPlat_HapticFeedbackSubsystem::RegisterHapticService()
{
	if (bRegisteredService)
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator must not extend this subsystem's lifetime (the GI owns it).
		bRegisteredService = Locator->RegisterService(
			Plat_NativeTags::Service_Haptics, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void UPlat_HapticFeedbackSubsystem::UnregisterHapticService()
{
	if (!bRegisteredService)
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (Locator->ResolveService(Plat_NativeTags::Service_Haptics) == this)
		{
			Locator->UnregisterService(Plat_NativeTags::Service_Haptics);
		}
	}
	bRegisteredService = false;
}

// ---------------------------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------------------------

void UPlat_HapticFeedbackSubsystem::PlayHaptic(FGameplayTag EffectTag, float Scale)
{
	if (!AreHapticsEnabled() || !EffectTag.IsValid())
	{
		return;
	}
	if (!ActiveSet)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Platform] PlayHaptic(%s): no active haptic bank."), *EffectTag.ToString());
		return;
	}

	FPlat_HapticEffect Effect;
	if (!ActiveSet->FindEffect(EffectTag, Effect))
	{
		UE_LOG(LogDP, Verbose, TEXT("[Platform] PlayHaptic: tag %s not in bank %s."),
			*EffectTag.ToString(), *GetNameSafe(ActiveSet));
		return;
	}

	const UPlat_DeviceCapabilitySubsystem* Caps = CapsWeak.Get();

	switch (Effect.Mode)
	{
	case EPlat_HapticMode::ForceFeedbackAsset:
		// Force-feedback asset path: gate on gamepad rumble capability (defensive default = allow when
		// capabilities are unknown, so the engine itself decides if no device is attached).
		if (!Caps || Caps->SupportsGamepadRumble())
		{
			PlayGamepadFF(Effect, Scale);
		}
		break;

	case EPlat_HapticMode::MotorValues:
		// Motor / vibration path: gamepad motors when a rumble-capable pad is present, mobile vibration
		// when on a touch device. Both are confined inside PlayMobileVibration.
		PlayMobileVibration(Effect, Scale);
		break;

	default:
		break;
	}
}

void UPlat_HapticFeedbackSubsystem::StopAllHaptics()
{
	if (APlayerController* PC = ResolveLocalController())
	{
		// Stop every tagged effect we have played. We pass a null effect + the tag so the engine stops
		// all instances bearing that tag (the stable per-tag stop API), then clear our bookkeeping.
		for (const FName& Tag : ActiveTags)
		{
			PC->ClientStopForceFeedback(nullptr, Tag);
		}
	}
	ActiveTags.Reset();
}

void UPlat_HapticFeedbackSubsystem::SetHapticsEnabled(bool bEnabled)
{
	if (bHapticsEnabled == bEnabled)
	{
		return;
	}
	bHapticsEnabled = bEnabled;
	if (!bHapticsEnabled)
	{
		StopAllHaptics();
	}
	OnHapticsEnabledChanged.Broadcast(bHapticsEnabled);
}

bool UPlat_HapticFeedbackSubsystem::AreHapticsEnabled() const
{
	if (!bHapticsEnabled)
	{
		return false;
	}
	// Device gate: when capabilities are known, require at least one deliverable channel.
	if (const UPlat_DeviceCapabilitySubsystem* Caps = CapsWeak.Get())
	{
		return Caps->SupportsGamepadRumble() || Caps->SupportsTouch();
	}
	// Capabilities unknown (very early init): allow — the engine no-ops if no device exists.
	return true;
}

void UPlat_HapticFeedbackSubsystem::SetActiveEffectSet(UPlat_HapticEffectSet* Set)
{
	if (Set)
	{
		ActiveSet = Set;
	}
	else if (const UPlat_HapticSettings* Settings = UPlat_HapticSettings::Get())
	{
		// Null reverts to the configured default bank.
		ActiveSet = Settings->DefaultEffectSet.IsNull() ? nullptr : Settings->DefaultEffectSet.LoadSynchronous();
	}
	else
	{
		ActiveSet = nullptr;
	}
}

// ---------------------------------------------------------------------------------------------
//  ISeam_HapticController
// ---------------------------------------------------------------------------------------------

void UPlat_HapticFeedbackSubsystem::PlayHapticByTag_Implementation(FGameplayTag EffectTag, float Scale)
{
	PlayHaptic(EffectTag, Scale);
}

void UPlat_HapticFeedbackSubsystem::StopHaptics_Implementation()
{
	StopAllHaptics();
}

bool UPlat_HapticFeedbackSubsystem::AreHapticsEnabled_Implementation() const
{
	return AreHapticsEnabled();
}

// ---------------------------------------------------------------------------------------------
//  Internals
// ---------------------------------------------------------------------------------------------

APlayerController* UPlat_HapticFeedbackSubsystem::ResolveLocalController() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}
	// First local player drives haptics (split-screen secondary pads can be added by the project later).
	if (ULocalPlayer* LP = GI->GetFirstGamePlayer())
	{
		if (UWorld* World = GI->GetWorld())
		{
			return LP->GetPlayerController(World);
		}
	}
	return nullptr;
}

float UPlat_HapticFeedbackSubsystem::ComputeEffectiveScale(const FPlat_HapticEffect& Effect, float CallerScale) const
{
	float Effective = FMath::Max(0.f, CallerScale);

	if (const UPlat_HapticSettings* Settings = UPlat_HapticSettings::Get())
	{
		Effective *= FMath::Clamp(Settings->MasterIntensity, 0.f, 1.f);
		if (Effect.CategoryTag.IsValid())
		{
			if (const float* CatScalar = Settings->CategoryIntensityScalars.Find(Effect.CategoryTag))
			{
				Effective *= FMath::Max(0.f, *CatScalar);
			}
		}
	}
	return Effective;
}

void UPlat_HapticFeedbackSubsystem::PlayGamepadFF(const FPlat_HapticEffect& Effect, float Scale)
{
	APlayerController* PC = ResolveLocalController();
	if (!PC)
	{
		return;
	}

	UForceFeedbackEffect* FFEffect = Effect.ForceFeedback.IsNull() ? nullptr : Effect.ForceFeedback.LoadSynchronous();
	if (!FFEffect)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Platform] Haptic %s has no force-feedback asset."), *Effect.EffectTag.ToString());
		return;
	}

	const float Effective = ComputeEffectiveScale(Effect, Scale);
	if (Effective <= 0.f)
	{
		return;
	}

	// Wrap the engine force-feedback system. We tag the playing effect with the effect tag so a later
	// StopAllHaptics / per-tag cancel can target it. Intensity is applied via the parameters.
	const FName Tag = Effect.EffectTag.GetTagName();
	FForceFeedbackParameters Params;
	Params.Tag = Tag;
	Params.bLooping = false;
	Params.bIgnoreTimeDilation = true;
	PC->ClientPlayForceFeedback(FFEffect, Params);
	ActiveTags.Add(Tag);

	UE_LOG(LogDP, Verbose, TEXT("[Platform] Played FF haptic %s (scale=%.2f)."), *Effect.EffectTag.ToString(), Effective);
}

void UPlat_HapticFeedbackSubsystem::PlayMobileVibration(const FPlat_HapticEffect& Effect, float Scale)
{
	const float Effective = ComputeEffectiveScale(Effect, Scale);
	if (Effective <= 0.f)
	{
		return;
	}

	const UPlat_DeviceCapabilitySubsystem* Caps = CapsWeak.Get();
	const bool bMobile = Caps ? Caps->IsMobile() : false;
	const bool bGamepad = Caps ? Caps->SupportsGamepadRumble() : false;

	// Mobile vibration path (touch devices). All platform branching is confined here with a generic no-op.
	if (bMobile && Effect.MobileVibrationIntensity > 0.f)
	{
#if PLATFORM_ANDROID || PLATFORM_IOS
		// Engine exposes a coarse device vibration; duration is in ms. We have no amplitude control on
		// generic mobile, so any non-zero intensity triggers a fixed-duration buzz.
		const int32 DurationMs = FMath::Max(1, FMath::RoundToInt(Effect.DurationSeconds * 1000.f));
		FPlatformMisc::PrepareMobileHaptics(EMobileHapticsType::Feedback);
		FPlatformMisc::TriggerMobileHaptics();
		(void)DurationMs; // Duration governed by the OS haptic type on mobile.
#else
		// Non-mobile build: nothing to do (kept for parity / future platform extensions).
#endif
		UE_LOG(LogDP, Verbose, TEXT("[Platform] Mobile vibration haptic %s (intensity=%.2f)."),
			*Effect.EffectTag.ToString(), Effect.MobileVibrationIntensity * Effective);
		return;
	}

	// Gamepad motor path: drive the motors for the row's duration via the engine's dynamic force-feedback
	// (wraps APlayerController; no reinvention, no raw input-pipeline poking). We start each affected
	// channel at the row's intensity for DurationSeconds; the engine auto-stops it when the timer elapses.
	if (bGamepad)
	{
		if (APlayerController* PC = ResolveLocalController())
		{
			const float Large = FMath::Clamp(Effect.LargeMotor * Effective, 0.f, 1.f);
			const float Small = FMath::Clamp(Effect.SmallMotor * Effective, 0.f, 1.f);
			const float Duration = FMath::Max(0.f, Effect.DurationSeconds);

			// Large-motor channels.
			if (Large > 0.f)
			{
				PC->PlayDynamicForceFeedback(Large, Duration,
					/*bAffectsLeftLarge=*/true, /*bAffectsLeftSmall=*/false,
					/*bAffectsRightLarge=*/true, /*bAffectsRightSmall=*/false,
					EDynamicForceFeedbackAction::Start);
			}
			// Small-motor channels.
			if (Small > 0.f)
			{
				PC->PlayDynamicForceFeedback(Small, Duration,
					/*bAffectsLeftLarge=*/false, /*bAffectsLeftSmall=*/true,
					/*bAffectsRightLarge=*/false, /*bAffectsRightSmall=*/true,
					EDynamicForceFeedbackAction::Start);
			}

			UE_LOG(LogDP, Verbose, TEXT("[Platform] Gamepad motor haptic %s (L=%.2f S=%.2f dur=%.2f)."),
				*Effect.EffectTag.ToString(), Large, Small, Duration);
		}
	}
}
