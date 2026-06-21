// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Seam_StatMod.generated.h"

/**
 * A single stat modifier expressed in seam-neutral terms, so any system (equipment affixes, set bonuses,
 * encumbrance, status effects, movement) can contribute attribute modifiers to a stats component without
 * depending on the RPG module's concrete FRPG_StatModifier type.
 *
 * Op is the integer value of the RPG module's ERPG_StatModOp (Additive / Multiplicative / Override) — the
 * mapping helper lives in RPG, not here, to keep Seams a leaf. Magnitude rides an FSeam_NetValue so a
 * modifier never replicates a raw FInstancedStruct.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_StatMod
{
	GENERATED_BODY()

	/** Which attribute this modifies (tag-keyed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Stats")
	FGameplayTag AttributeTag;

	/** Combine operation: the integer value of ERPG_StatModOp (0=Additive, 1=Multiplicative, 2=Override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Stats")
	uint8 Op = 0;

	/** The modifier magnitude (use FSeam_NetValue::MakeFloat for the common numeric case). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Stats")
	FSeam_NetValue Magnitude;

	/** The source that granted this modifier, so a whole group can be removed/replaced together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Stats")
	FGameplayTag SourceTag;

	FSeam_StatMod() = default;

	FSeam_StatMod(const FGameplayTag& InAttribute, uint8 InOp, double InMagnitude, const FGameplayTag& InSource)
		: AttributeTag(InAttribute)
		, Op(InOp)
		, Magnitude(FSeam_NetValue::MakeFloat(InMagnitude))
		, SourceTag(InSource)
	{
	}
};
