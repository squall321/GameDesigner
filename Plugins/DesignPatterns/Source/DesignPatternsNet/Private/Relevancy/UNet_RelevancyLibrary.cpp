// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Relevancy/UNet_RelevancyLibrary.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

void UNet_RelevancyLibrary::ApplyNetUpdateFrequency(AActor* Actor, float FrequencyHz, float MinHz, float MaxHz)
{
	if (!UNet_NetUtilsLibrary::HasAuthority(Actor))
	{
		return;
	}
	const float Clamped = FMath::Clamp(FrequencyHz, FMath::Max(0.1f, MinHz), FMath::Max(MinHz, MaxHz));

	// The plugin targets UE 5.3-5.5 where NetUpdateFrequency remains a public, assignable field (the 5.5
	// SetNetUpdateFrequency accessor wraps the same field). Mirror the rest of the plugin, which assigns it
	// directly, so behaviour is identical across the supported range.
	Actor->NetUpdateFrequency = Clamped;
}

void UNet_RelevancyLibrary::ApplyNetCullDistance(AActor* Actor, float DistanceUU, float MinDist, float MaxDist)
{
	if (!UNet_NetUtilsLibrary::HasAuthority(Actor))
	{
		return;
	}
	const float Clamped = FMath::Clamp(DistanceUU, FMath::Max(0.f, MinDist), FMath::Max(MinDist, MaxDist));

	// The engine stores the SQUARED cull distance. NetCullDistanceSquared is a public, assignable field
	// across UE 5.3-5.5 (the 5.5 accessor wraps the same field); assign it directly for version stability.
	Actor->NetCullDistanceSquared = (float)((double)Clamped * (double)Clamped);
}

void UNet_RelevancyLibrary::SetActorDormant(AActor* Actor, bool bDormant)
{
	if (!UNet_NetUtilsLibrary::HasAuthority(Actor))
	{
		return;
	}
	if (bDormant)
	{
		Actor->SetNetDormancy(DORM_DormantAll);
	}
	else
	{
		Actor->SetNetDormancy(DORM_Awake);
		Actor->ForceNetUpdate();
	}
}

void UNet_RelevancyLibrary::WakeDormantActor(AActor* Actor)
{
	if (!UNet_NetUtilsLibrary::HasAuthority(Actor) || !Actor)
	{
		return;
	}
	if (Actor->NetDormancy > DORM_Awake)
	{
		Actor->FlushNetDormancy();
	}
	Actor->ForceNetUpdate();
}

ESeam_NetRelevancyTier UNet_RelevancyLibrary::ResolveRelevancyTier(const AActor* Actor, ESeam_NetRelevancyTier DefaultTier)
{
	if (Actor && Actor->Implements<USeam_NetRelevancyHint>())
	{
		return ISeam_NetRelevancyHint::Execute_GetRelevancyTier(Actor);
	}
	return DefaultTier;
}
