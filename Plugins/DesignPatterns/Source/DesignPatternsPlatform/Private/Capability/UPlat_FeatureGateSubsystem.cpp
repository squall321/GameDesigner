// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Capability/UPlat_FeatureGateSubsystem.h"
#include "Capability/UPlat_FeatureGateSettings.h"

#include "Core/DPLog.h"

// ---------------------------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------------------------

void UPlat_FeatureGateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bLastOnline = ComputePlatformOnline();
	UE_LOG(LogDP, Log, TEXT("[Platform] FeatureGateSubsystem initialized (online=%d)."), bLastOnline ? 1 : 0);
}

void UPlat_FeatureGateSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

FString UPlat_FeatureGateSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Features online=%d store=%d presence=%d"),
		IsOnline() ? 1 : 0, IsStoreAvailable() ? 1 : 0, IsPresenceAvailable() ? 1 : 0);
}

// ---------------------------------------------------------------------------------------------
//  Platform probing (confined)
// ---------------------------------------------------------------------------------------------

bool UPlat_FeatureGateSubsystem::ComputePlatformOnline() const
{
#if WITH_EDITOR
	if (const UPlat_FeatureGateSettings* Settings = UPlat_FeatureGateSettings::Get())
	{
		if (Settings->bAssumeOnlineInEditor)
		{
			return true;
		}
	}
#endif
	// Generic fallback: assume online. A project's online layer pushes the real state via
	// NotifyOnlineState; precise OSS connectivity belongs in a platform extension behind WITH_*.
	return true;
}

void UPlat_FeatureGateSubsystem::NotifyOnlineState(bool bOnline)
{
	if (bOnline != bLastOnline)
	{
		bLastOnline = bOnline;
		OnOnlineStateChanged.Broadcast(bOnline);
		UE_LOG(LogDP, Verbose, TEXT("[Platform] Online state changed to %d."), bOnline ? 1 : 0);
	}
}

// ---------------------------------------------------------------------------------------------
//  Queries
// ---------------------------------------------------------------------------------------------

bool UPlat_FeatureGateSubsystem::IsOnline() const
{
	return bLastOnline;
}

bool UPlat_FeatureGateSubsystem::IsStoreAvailable() const
{
	// A store needs online; concrete store availability is a platform extension concern. Generic
	// fallback: store is available when online.
	return IsOnline();
}

bool UPlat_FeatureGateSubsystem::IsPresenceAvailable() const
{
	// Presence needs online; generic fallback ties it to online state.
	return IsOnline();
}

bool UPlat_FeatureGateSubsystem::IsFeatureAvailable(FGameplayTag FeatureTag) const
{
	// 1) Hard override from settings wins.
	if (const UPlat_FeatureGateSettings* Settings = UPlat_FeatureGateSettings::Get())
	{
		if (const bool* Override = Settings->FeatureOverrides.Find(FeatureTag))
		{
			return *Override;
		}
	}

	// 2) Compute a platform default (generic fallback: available when online).
	const bool bPlatformDefault = IsOnline();

	// 3) Let the project veto/override.
	return OnQueryFeature(FeatureTag, bPlatformDefault);
}

bool UPlat_FeatureGateSubsystem::OnQueryFeature_Implementation(FGameplayTag /*FeatureTag*/, bool bPlatformDefault) const
{
	return bPlatformDefault;
}
