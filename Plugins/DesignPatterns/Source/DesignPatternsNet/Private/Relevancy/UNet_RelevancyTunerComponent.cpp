// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Relevancy/UNet_RelevancyTunerComponent.h"
#include "Relevancy/UNet_RelevancyLibrary.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

UNet_RelevancyTunerComponent::UNet_RelevancyTunerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNet_RelevancyTunerComponent::BeginPlay()
{
	Super::BeginPlay();
	// Apply once on the authority as the actor enters play.
	ApplyNow();
}

FNet_RelevancyTierConfig UNet_RelevancyTunerComponent::ResolveTierConfig(ESeam_NetRelevancyTier Tier) const
{
	if (const FNet_RelevancyTierConfig* Found = TierConfigs.FindByPredicate(
		[Tier](const FNet_RelevancyTierConfig& C){ return C.Tier == Tier; }))
	{
		return *Found;
	}

	// Defensive fallback when the table omits the tier: scale a Normal baseline by tier priority so behaviour
	// is still ordered (Critical fastest/farthest, Dormant slowest). These are conservative defaults the
	// project is expected to override via TierConfigs.
	FNet_RelevancyTierConfig Fallback;
	Fallback.Tier = Tier;
	switch (Tier)
	{
	case ESeam_NetRelevancyTier::Critical: Fallback.NetUpdateFrequencyHz = 60.f; Fallback.NetCullDistanceUU = 150000.f; Fallback.bAllowDormancy = false; break;
	case ESeam_NetRelevancyTier::High:     Fallback.NetUpdateFrequencyHz = 30.f; Fallback.NetCullDistanceUU = 100000.f; Fallback.bAllowDormancy = false; break;
	case ESeam_NetRelevancyTier::Normal:   Fallback.NetUpdateFrequencyHz = 10.f; Fallback.NetCullDistanceUU =  50000.f; Fallback.bAllowDormancy = false; break;
	case ESeam_NetRelevancyTier::Low:      Fallback.NetUpdateFrequencyHz =  3.f; Fallback.NetCullDistanceUU =  25000.f; Fallback.bAllowDormancy = true;  break;
	case ESeam_NetRelevancyTier::Dormant:  Fallback.NetUpdateFrequencyHz =  1.f; Fallback.NetCullDistanceUU =  15000.f; Fallback.bAllowDormancy = true;  break;
	}
	return Fallback;
}

void UNet_RelevancyTunerComponent::ApplyNow()
{
	AActor* Owner = GetOwner();
	if (!UNet_NetUtilsLibrary::HasAuthority(Owner))
	{
		return; // relevancy/dormancy are authority-only concepts
	}

	const ESeam_NetRelevancyTier Tier = UNet_RelevancyLibrary::ResolveRelevancyTier(Owner, DefaultTier);
	const FNet_RelevancyTierConfig Config = ResolveTierConfig(Tier);

	// Per-actor cull bias from the hint (additive), then clamp.
	float CullBias = 0.f;
	bool bWantsDormancy = false;
	if (Owner->Implements<USeam_NetRelevancyHint>())
	{
		CullBias = ISeam_NetRelevancyHint::Execute_GetCullDistanceBias(Owner);
		bWantsDormancy = ISeam_NetRelevancyHint::Execute_WantsDormancyWhenIdle(Owner);
	}

	UNet_RelevancyLibrary::ApplyNetUpdateFrequency(Owner, Config.NetUpdateFrequencyHz, MinFrequencyHz, MaxFrequencyHz);
	UNet_RelevancyLibrary::ApplyNetCullDistance(Owner, Config.NetCullDistanceUU + CullBias, MinCullDistanceUU, MaxCullDistanceUU);

	// Only put the actor dormant when BOTH the tier permits it AND the actor wants it — otherwise keep it
	// awake. We never force-wake here (waking is a per-change concern handled by the carrier's WakeForChange).
	if (Config.bAllowDormancy && bWantsDormancy)
	{
		UNet_RelevancyLibrary::SetActorDormant(Owner, true);
	}

	UE_LOG(LogDP, Verbose, TEXT("RelevancyTuner[%s]: tier=%d freq=%.1f cull=%.0f dormant=%d."),
		*Owner->GetName(), (int32)Tier, Config.NetUpdateFrequencyHz, Config.NetCullDistanceUU + CullBias,
		(Config.bAllowDormancy && bWantsDormancy) ? 1 : 0);
}
