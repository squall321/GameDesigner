// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Effect/Combat_StatusEffect.h"
#include "Effect/Combat_StatusEffectComponent.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

UWorld* UCombat_StatusEffect::GetWorld() const
{
	// Route world resolution through the owning component's actor so timers/traces work.
	if (const UCombat_StatusEffectComponent* Comp = GetOwningComponent())
	{
		if (const AActor* Owner = Comp->GetOwner())
		{
			return Owner->GetWorld();
		}
	}
	// CDO / no outer: avoid asserting in the editor.
	return nullptr;
}

UCombat_StatusEffectComponent* UCombat_StatusEffect::GetOwningComponent() const
{
	return Cast<UCombat_StatusEffectComponent>(GetOuter());
}

void UCombat_StatusEffect::OnApply_Implementation(AActor* Target)
{
	UE_LOG(LogDP, Verbose, TEXT("[Combat] Status %s applied to %s"),
		*EffectTag.ToString(), *GetNameSafe(Target));
}

void UCombat_StatusEffect::OnTick_Implementation(AActor* Target, float DeltaTime)
{
	// Base class does nothing measurable; DoT subclasses apply damage here.
}

void UCombat_StatusEffect::OnRemove_Implementation(AActor* Target, bool bExpiredNaturally)
{
	UE_LOG(LogDP, Verbose, TEXT("[Combat] Status %s removed from %s (%s)"),
		*EffectTag.ToString(), *GetNameSafe(Target),
		bExpiredNaturally ? TEXT("expired") : TEXT("forced"));
}
