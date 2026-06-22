// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Trait/Ent_AdvancedTrait.h"
#include "Stats/Seam_StatMod.h"
#include "Ent_StatContributionTrait.generated.h"

/**
 * An advanced trait that contributes a group of stat modifiers via the LOCAL-DERIVED seam path.
 *
 * It recomputes its modifier group from REPLICATED inputs only (the enabled flag + stack count carried
 * in FEnt_TraitEntry::StatePayload) and pushes through ISeam_StatModifierSink::SetDerivedModifierGroup,
 * which by contract has NO authority guard and runs on server AND clients. Because the inputs are
 * replicated, server and clients derive the identical group — no desync (this is exactly the pattern
 * the stat seam was designed for).
 *
 * The stat sink is resolved EACH USE via the entity's IEnt_CapabilityProvider (GetCapabilityObject for
 * the configured StatSinkCapabilityTag); the pointer is never cached, per the capability-seam contract.
 *
 * When disabled, the trait pushes an EMPTY group under its SourceTag (clearing its contribution) rather
 * than leaving a stale group behind.
 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Entity Stat-Contribution Trait"))
class DESIGNPATTERNSENTITY_API UEnt_StatContributionTrait : public UEnt_AdvancedTrait
{
	GENERATED_BODY()

public:
	UEnt_StatContributionTrait();

	/** The base modifiers this trait grants (per-stack; scaled by the replicated stack count). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|StatContribution")
	TArray<FSeam_StatMod> AuthoredMods;

	/** The source tag this trait's modifier group is keyed under (so it can be replaced atomically). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|StatContribution")
	FGameplayTag SourceTag;

	/**
	 * Capability tag whose backing object implements ISeam_StatModifierSink (the entity's stats sink).
	 * Resolved per-use via the capability seam; never cached.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|StatContribution", meta = (Categories = "Ent.Cap"))
	FGameplayTag StatSinkCapabilityTag;

	/**
	 * When true, each modifier's Float magnitude is multiplied by the stack count before being pushed.
	 * Non-numeric magnitudes (tags/names) are passed through unscaled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|StatContribution")
	bool bScaleByStack = true;

	/** Recompute the modifier group from the replicated enabled/stack inputs and push it to the sink. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|StatContribution")
	void RecomputeAndPush();

	//~ Begin UEnt_Trait / UEnt_AdvancedTrait.
	virtual void OnTraitAdded_Implementation(UEnt_EntityComponent* OwningComponent_In) override;
	virtual void OnTraitRemoved_Implementation() override;
	//~ End.

protected:
	//~ Begin UEnt_AdvancedTrait.
	virtual void OnTraitEnabled_Implementation() override;
	virtual void OnTraitDisabled_Implementation() override;
	//~ End.

private:
	/** Bound to the owning component's OnEntityChanged so a replicated StatePayload change re-derives. */
	UFUNCTION()
	void HandleEntityChanged(UEnt_EntityComponent* Component);

	/** Whether we have bound HandleEntityChanged (so we unbind exactly once). */
	bool bBoundToEntityChanged = false;
};
