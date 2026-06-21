// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Hit/Combat_DamageExecution.h"
#include "Combat_IFrameAwareDamageExecution.generated.h"

/**
 * The lightweight i-frame enforcement Strategy — the SINGLE owner of the i-frame overlap.
 *
 * It extends the base UCombat_DamageExecution and returns 0 damage whenever the victim's
 * UDP_GameplayActionComponent carries the shared invulnerability owned-tag (DP.Combat.Status.IFrame).
 * Combat's dodge and Movement's dash both merely ADD that tag — neither needs its own seam, which is
 * why the spec deletes the ISeam_IFrameSource overlap and resolves it here, in Combat (Combat owns
 * the DamageExecution extension point).
 *
 * Otherwise it falls through to the base implementation (BaseDamage + crit), so it is a drop-in,
 * minimal wrapper a designer can assign to a hitbox when full pipeline mitigation is not wanted but
 * i-frames still must be honored. CONST/pure like every execution.
 */
UCLASS(meta = (DisplayName = "I-Frame Aware Damage Execution"))
class DESIGNPATTERNSCOMBAT_API UCombat_IFrameAwareDamageExecution : public UCombat_DamageExecution
{
	GENERATED_BODY()

public:
	//~ Begin UCombat_DamageExecution
	/** Returns 0 if the victim has the i-frame owned-tag; otherwise the base damage. PURE. */
	virtual float CalculateDamage_Implementation(const FCombat_HitResult& Hit) const override;
	//~ End UCombat_DamageExecution
};
