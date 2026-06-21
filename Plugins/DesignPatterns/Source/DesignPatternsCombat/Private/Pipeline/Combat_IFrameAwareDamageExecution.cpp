// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pipeline/Combat_IFrameAwareDamageExecution.h"
#include "Combat_DeepNativeTags.h"

#include "Action/DPGameplayActionComponent.h"
#include "Hit/Combat_HitTypes.h"

#include "GameFramework/Actor.h"

float UCombat_IFrameAwareDamageExecution::CalculateDamage_Implementation(const FCombat_HitResult& Hit) const
{
	if (const AActor* Victim = Hit.HitActor.Get())
	{
		if (const UDP_GameplayActionComponent* Action = Victim->FindComponentByClass<UDP_GameplayActionComponent>())
		{
			if (Action->GetOwnedTags().HasTag(CombatDeepNativeTags::Status_IFrame))
			{
				return 0.f; // invulnerable — fully negate the hit
			}
		}
	}

	// Not invulnerable: defer to the base crit/passthrough formula.
	return Super::CalculateDamage_Implementation(Hit);
}
