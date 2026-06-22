// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Capability/UPlat_ScalabilitySubsystem.h"

#include "Core/DPLog.h"
#include "Engine/GameInstance.h"
#include "Scalability.h"
#include "HAL/IConsoleManager.h"

// ---------------------------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------------------------

bool UPlat_ScalabilitySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Scalability is rendering-only; skip dedicated-server creation.
	return !IsRunningDedicatedServer();
}

void UPlat_ScalabilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CapsWeak = GetGameInstance() ? GetGameInstance()->GetSubsystem<UPlat_DeviceCapabilitySubsystem>() : nullptr;
	UE_LOG(LogDP, Log, TEXT("[Platform] ScalabilitySubsystem initialized."));
}

void UPlat_ScalabilitySubsystem::Deinitialize()
{
	ActiveProfile = nullptr;
	CapsWeak.Reset();
	Super::Deinitialize();
}

FString UPlat_ScalabilitySubsystem::GetDPDebugString_Implementation() const
{
	const EPlat_PerfTier Tier = CapsWeak.IsValid() ? CapsWeak->GetPerfTier() : EPlat_PerfTier::High;
	return FString::Printf(TEXT("Scalability tier=%d profile=%s"), (int32)Tier, *GetNameSafe(ActiveProfile));
}

// ---------------------------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------------------------

void UPlat_ScalabilitySubsystem::SetActiveProfile(UPlat_ScalabilityProfile* Profile)
{
	ActiveProfile = Profile;
}

void UPlat_ScalabilitySubsystem::ApplyProfileForCurrentTier()
{
	UPlat_DeviceCapabilitySubsystem* Caps = CapsWeak.Get();
	if (!Caps)
	{
		Caps = GetGameInstance() ? GetGameInstance()->GetSubsystem<UPlat_DeviceCapabilitySubsystem>() : nullptr;
		CapsWeak = Caps;
	}

	const EPlat_PerfTier Tier = Caps ? Caps->GetPerfTier() : EPlat_PerfTier::High;

	// COMPOSE: let the capability subsystem push its baseline scalability hints (running the project's
	// OnApplyScalability hook) FIRST, so we layer the authored bucket on top instead of bypassing it.
	if (Caps)
	{
		Caps->ApplyScalabilityHints();
	}

	if (!ActiveProfile)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Platform] ApplyProfileForCurrentTier: no active profile; baseline hints only."));
		OnScalabilityApplied.Broadcast(Tier);
		return;
	}

	FPlat_ScalabilityBucket Bucket;
	if (ActiveProfile->FindBucket(Tier, Bucket))
	{
		ApplyBucket(Bucket);
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("[Platform] No scalability bucket for tier %d in profile %s."),
			(int32)Tier, *GetNameSafe(ActiveProfile));
		OnScalabilityApplied.Broadcast(Tier);
	}
}

void UPlat_ScalabilitySubsystem::ApplyBucket(const FPlat_ScalabilityBucket& Bucket)
{
	ApplyQualityLevels(Bucket);
	ApplyDynamicResolution(Bucket);
	OnScalabilityApplied.Broadcast(Bucket.Tier);
	UE_LOG(LogDP, Log, TEXT("[Platform] Applied scalability bucket for tier %d."), (int32)Bucket.Tier);
}

void UPlat_ScalabilitySubsystem::SetDynamicResolutionEnabled(bool bEnabled)
{
	SetIntCVar(TEXT("r.DynamicRes.OperationMode"), bEnabled ? 2 : 0);
}

void UPlat_ScalabilitySubsystem::StepScalability(int32 Delta)
{
	if (Delta == 0)
	{
		return;
	}
	// Read the engine's current levels, shift every group, clamp and re-apply (wraps the engine).
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	auto Shift = [Delta](int32 In) { return FMath::Clamp(In + Delta, 0, 4); };

	Levels.SetViewDistanceQuality(Shift(Levels.ViewDistanceQuality));
	Levels.SetAntiAliasingQuality(Shift(Levels.AntiAliasingQuality));
	Levels.SetShadowQuality(Shift(Levels.ShadowQuality));
	Levels.SetGlobalIlluminationQuality(Shift(Levels.GlobalIlluminationQuality));
	Levels.SetReflectionQuality(Shift(Levels.ReflectionQuality));
	Levels.SetPostProcessQuality(Shift(Levels.PostProcessQuality));
	Levels.SetTextureQuality(Shift(Levels.TextureQuality));
	Levels.SetEffectsQuality(Shift(Levels.EffectsQuality));
	Levels.SetFoliageQuality(Shift(Levels.FoliageQuality));
	Levels.SetShadingQuality(Shift(Levels.ShadingQuality));

	Scalability::SetQualityLevels(Levels);
}

// ---------------------------------------------------------------------------------------------
//  Internals (engine wrappers)
// ---------------------------------------------------------------------------------------------

void UPlat_ScalabilitySubsystem::ApplyQualityLevels(const FPlat_ScalabilityBucket& Bucket)
{
	// Start from the engine's current levels so any field we don't author is preserved.
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();

	auto C = [](int32 In) { return FMath::Clamp(In, 0, 4); };

	Levels.SetViewDistanceQuality(C(Bucket.ViewDistance));
	Levels.SetAntiAliasingQuality(C(Bucket.AntiAliasing));
	Levels.SetShadowQuality(C(Bucket.Shadow));
	Levels.SetGlobalIlluminationQuality(C(Bucket.GlobalIllumination));
	Levels.SetReflectionQuality(C(Bucket.Reflection));
	Levels.SetPostProcessQuality(C(Bucket.PostProcess));
	Levels.SetTextureQuality(C(Bucket.Texture));
	Levels.SetEffectsQuality(C(Bucket.Effects));
	Levels.SetFoliageQuality(C(Bucket.Foliage));
	Levels.SetShadingQuality(C(Bucket.Shading));

	Scalability::SetQualityLevels(Levels);
}

void UPlat_ScalabilitySubsystem::ApplyDynamicResolution(const FPlat_ScalabilityBucket& Bucket)
{
	SetDynamicResolutionEnabled(Bucket.bEnableDynamicResolution);
	if (Bucket.bEnableDynamicResolution)
	{
		const float MinPct = FMath::Clamp(Bucket.DynResMinScreenPercentage, 0.25f, 1.f) * 100.f;
		const float MaxPct = FMath::Clamp(Bucket.DynResMaxScreenPercentage, 0.25f, 1.f) * 100.f;
		// Engine CVars take screen percentage in [0,100].
		SetFloatCVar(TEXT("r.DynamicRes.MinScreenPercentage"), FMath::Min(MinPct, MaxPct));
		SetFloatCVar(TEXT("r.DynamicRes.MaxScreenPercentage"), FMath::Max(MinPct, MaxPct));
	}
}

void UPlat_ScalabilitySubsystem::SetFloatCVar(const TCHAR* Name, float Value)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		CVar->Set(Value, ECVF_SetByGameSetting);
	}
}

void UPlat_ScalabilitySubsystem::SetIntCVar(const TCHAR* Name, int32 Value)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		CVar->Set(Value, ECVF_SetByGameSetting);
	}
}
