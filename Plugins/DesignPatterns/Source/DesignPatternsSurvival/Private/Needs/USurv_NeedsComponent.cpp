// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Needs/USurv_NeedsComponent.h"
#include "Needs/USurv_HealthSinkInterface.h"
#include "DesignPatternsSurvivalTags.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

USurv_NeedsComponent::USurv_NeedsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);

	// Sensible defaults so the component is useful out of the box; designers tune per-actor.
	Meters.Add(FSurv_NeedMeter{ ESurv_NeedType::Hunger,      100.f, 100.f, 0.5f, 10.f, false });
	Meters.Add(FSurv_NeedMeter{ ESurv_NeedType::Thirst,      100.f, 100.f, 0.8f, 10.f, false });
	Meters.Add(FSurv_NeedMeter{ ESurv_NeedType::Stamina,     100.f, 100.f, 0.0f, 10.f, false });
	Meters.Add(FSurv_NeedMeter{ ESurv_NeedType::Temperature, 100.f, 100.f, 0.0f, 10.f, false });
}

void USurv_NeedsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USurv_NeedsComponent, Meters);
}

void USurv_NeedsComponent::BeginPlay()
{
	Super::BeginPlay();
	// Only the authority drives drain; clients receive values via replication.
	PrimaryComponentTick.SetTickFunctionEnable(HasAuthorityToMutate());
}

bool USurv_NeedsComponent::HasAuthorityToMutate() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

FSurv_NeedMeter* USurv_NeedsComponent::FindMeter(ESurv_NeedType Type)
{
	return Meters.FindByPredicate([Type](const FSurv_NeedMeter& M) { return M.Type == Type; });
}

const FSurv_NeedMeter* USurv_NeedsComponent::FindMeter(ESurv_NeedType Type) const
{
	return Meters.FindByPredicate([Type](const FSurv_NeedMeter& M) { return M.Type == Type; });
}

float USurv_NeedsComponent::GetNeed(ESurv_NeedType Type) const
{
	const FSurv_NeedMeter* M = FindMeter(Type);
	return M ? M->Current : 0.f;
}

float USurv_NeedsComponent::GetNeedNormalized(ESurv_NeedType Type) const
{
	const FSurv_NeedMeter* M = FindMeter(Type);
	return (M && M->Max > 0.f) ? FMath::Clamp(M->Current / M->Max, 0.f, 1.f) : 0.f;
}

bool USurv_NeedsComponent::IsNeedCritical(ESurv_NeedType Type) const
{
	const FSurv_NeedMeter* M = FindMeter(Type);
	return M ? (M->Current <= M->CriticalThreshold) : false;
}

void USurv_NeedsComponent::ApplyNeedDelta(ESurv_NeedType Type, float Delta)
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	FSurv_NeedMeter* M = FindMeter(Type);
	if (!M)
	{
		return;
	}
	const float Old = M->Current;
	M->Current = FMath::Clamp(M->Current + Delta, 0.f, M->Max);
	if (!FMath::IsNearlyEqual(Old, M->Current))
	{
		OnNeedChanged.Broadcast(Type, M->Current);
	}
}

FGameplayTag USurv_NeedsComponent::NeedToTag(ESurv_NeedType Type)
{
	switch (Type)
	{
	case ESurv_NeedType::Hunger:      return FGameplayTag::RequestGameplayTag(TEXT("Surv.Need.Hunger"), false);
	case ESurv_NeedType::Thirst:      return FGameplayTag::RequestGameplayTag(TEXT("Surv.Need.Thirst"), false);
	case ESurv_NeedType::Stamina:     return FGameplayTag::RequestGameplayTag(TEXT("Surv.Need.Stamina"), false);
	case ESurv_NeedType::Temperature: return FGameplayTag::RequestGameplayTag(TEXT("Surv.Need.Temperature"), false);
	default:                          return SurvNativeTags::Need;
	}
}

void USurv_NeedsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Defensive: never mutate replicated state off the authority even if a tick slips through.
	if (!HasAuthorityToMutate())
	{
		return;
	}

	bool bAnyCritical = false;

	for (FSurv_NeedMeter& M : Meters)
	{
		const float Old = M.Current;
		M.Current = FMath::Clamp(M.Current - M.DrainPerSecond * DeltaTime, 0.f, M.Max);
		if (!FMath::IsNearlyEqual(Old, M.Current))
		{
			OnNeedChanged.Broadcast(M.Type, M.Current);
		}

		const bool bCritical = M.Current <= M.CriticalThreshold;
		if (bCritical && !M.bWasCritical)
		{
			UE_LOG(LogDP, Verbose, TEXT("[Survival] Need critical: %d"), (int32)M.Type);
			OnNeedCritical.Broadcast(M.Type);
		}
		M.bWasCritical = bCritical;
		bAnyCritical |= bCritical;
	}

	// Accumulate toward periodic critical damage; one application per interval per critical meter.
	if (bAnyCritical && CriticalDamagePerTick > 0.f)
	{
		DamageAccumulator += DeltaTime;
		while (DamageAccumulator >= CriticalDamageInterval)
		{
			DamageAccumulator -= CriticalDamageInterval;
			for (const FSurv_NeedMeter& M : Meters)
			{
				if (M.Current <= M.CriticalThreshold)
				{
					ApplyCriticalDamage(CriticalDamagePerTick, M.Type);
				}
			}
		}
	}
	else
	{
		DamageAccumulator = 0.f;
	}
}

void USurv_NeedsComponent::ApplyCriticalDamage(float DamageAmount, ESurv_NeedType SourceNeed)
{
	const FGameplayTag SourceTag = NeedToTag(SourceNeed);

	// SOFT seam: if the owner implements the health-sink interface, route damage there.
	if (AActor* Owner = GetOwner())
	{
		if (Owner->Implements<USurv_HealthSinkInterface>())
		{
			ISurv_HealthSinkInterface::Execute_ApplyNeedsDamage(Owner, DamageAmount, SourceTag);
		}
	}

	// Always broadcast so projects without the interface can bind their own damage handler.
	OnNeedsDamage.Broadcast(DamageAmount, SourceTag);
}

void USurv_NeedsComponent::OnRep_Meters()
{
	// Clients translate replicated meter changes into per-need change events for UI.
	for (const FSurv_NeedMeter& M : Meters)
	{
		OnNeedChanged.Broadcast(M.Type, M.Current);
	}
}
