// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Combat/Seam_DamageReactor.h"

void ISeam_DamageReactor::OnDamageResolved_Implementation(AActor* /*Instigator*/, float /*Mitigated*/, FGameplayTag /*DamageTypeTag*/, FGameplayTag /*ReactionTag*/)
{
	// Inert native default for the damage-reactor seam. With no reactor components registered the
	// notification is a harmless no-op, so a project with no audio/VFX/quest reactors still links.
	// The real consumers are gameplay systems (AI threat, audio, VFX, quests) which override to react.
}

void ISeam_DamageReactor::OnDefeated_Implementation(AActor* /*Killer*/)
{
	// Inert native default for the defeat-reactor seam. With no reactor components registered the
	// notification is a harmless no-op, so a project with no defeat reactors still links.
	// The real consumers are gameplay systems (audio, VFX, quest completion) which override to react.
}
