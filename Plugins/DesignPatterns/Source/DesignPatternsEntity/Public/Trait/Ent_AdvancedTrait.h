// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Trait/Ent_Trait.h"
#include "Ent_AdvancedTrait.generated.h"

class UEnt_TraitDefinition;

/**
 * Optional richer trait base extending the shipped UEnt_Trait with dependency/conflict checks and a
 * runtime enable/stack state that is mirrored to clients.
 *
 * STATE SYNC (the critical correctness rule)
 *  The runtime "enabled" flag and "stack count" are NOT stored only in this subobject (subobjects of a
 *  non-replicated/replicated component do not reliably replicate their inner fields). Instead the
 *  authoritative state is written into the entity's REPLICATED FEnt_TraitEntry::StatePayload via
 *  UEnt_EntityComponent::SetTraitStatePayload, and IsTraitEnabled/GetStackCount READ it back. Both
 *  server and clients therefore observe identical enable/stack state, which is what derived contributors
 *  (UEnt_StatContributionTrait) depend on to avoid desync.
 *
 *  The StatePayload encodes both flags in one Int net-value: bit 0 = enabled, bits 1.. = stack count.
 *
 * DEPENDENCIES / CONFLICTS
 *  Resolved from the soft Definition against the owning entity's current trait set. A conflicting or
 *  unmet-dependency trait stays DISABLED (it still exists, but contributes nothing) rather than being
 *  silently dropped.
 *
 * CAPABILITY SUPPRESSION
 *  GetProvidedCapabilities returns the empty set while disabled, so a disabled trait advertises nothing
 *  through the capability seam.
 *
 * Existing traits keep deriving from UEnt_Trait untouched; this is a strictly additive sibling base.
 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Entity Advanced Trait"))
class DESIGNPATTERNSENTITY_API UEnt_AdvancedTrait : public UEnt_Trait
{
	GENERATED_BODY()

public:
	UEnt_AdvancedTrait();

	/** Design-time policy for this trait kind (dependencies/conflicts/priority/stacking). Soft ref. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|AdvancedTrait")
	TSoftObjectPtr<UEnt_TraitDefinition> Definition;

	/** Whether the trait starts enabled when added (subject to dependency/conflict checks). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|AdvancedTrait")
	bool bStartEnabled = true;

	/**
	 * Set the enabled state. AUTHORITY ONLY (writes the replicated StatePayload through the owner).
	 * No-op on clients. Enabling re-checks dependencies/conflicts and refuses if unmet.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|AdvancedTrait")
	void SetTraitEnabled(bool bEnabled);

	/** True if the trait is currently enabled (reads the replicated StatePayload; correct on clients). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Entity|AdvancedTrait")
	bool IsTraitEnabled() const;

	/** Current stack count (reads the replicated StatePayload), minimum 1. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Entity|AdvancedTrait")
	int32 GetStackCount() const;

	/**
	 * AUTHORITY ONLY. Set the stack count (clamped to the definition's MaxStackCount), writing the
	 * replicated StatePayload. No-op on clients.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|AdvancedTrait")
	void SetStackCount(int32 NewCount);

	/** True if the owning entity currently satisfies this trait's dependencies and has no conflict. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Entity|AdvancedTrait")
	bool AreDependenciesSatisfied() const;

	//~ Begin UEnt_Trait.
	virtual void OnTraitAdded_Implementation(UEnt_EntityComponent* OwningComponent_In) override;
	virtual void OnTraitRemoved_Implementation() override;
	virtual void GetProvidedCapabilities_Implementation(FGameplayTagContainer& OutCapabilities) const override;
	//~ End UEnt_Trait.

protected:
	/** Hook called (server + clients) after the trait becomes enabled. Override for cosmetic reactions. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|AdvancedTrait")
	void OnTraitEnabled();
	virtual void OnTraitEnabled_Implementation() {}

	/** Hook called (server + clients) after the trait becomes disabled. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|AdvancedTrait")
	void OnTraitDisabled();
	virtual void OnTraitDisabled_Implementation() {}

	/** Resolve the definition synchronously (loads the soft ref if needed); may be null. */
	const UEnt_TraitDefinition* GetDefinition() const;

	/** Encode enabled+stack into the single Int StatePayload (bit 0 = enabled, >>1 = stack). */
	static FSeam_NetValue EncodeState(bool bEnabled, int32 StackCount);

	/** Decode the enabled flag from a StatePayload (defaults to enabled when unset). */
	static bool DecodeEnabled(const FSeam_NetValue& Payload);

	/** Decode the stack count from a StatePayload (minimum 1). */
	static int32 DecodeStack(const FSeam_NetValue& Payload);

	/** Push the current (bEnabled, Stack) into the replicated entry (authority only). */
	void WriteState(bool bEnabled, int32 StackCount);

	/** Last enabled state we observed, so client-side rep changes can fire the right hook exactly once. */
	UPROPERTY(Transient)
	bool bLastObservedEnabled = false;

	/** Cached resolved definition (loaded once). */
	UPROPERTY(Transient)
	mutable TObjectPtr<UEnt_TraitDefinition> CachedDefinition;
};
