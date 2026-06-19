// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Action/DPGameplayActionInterface.h"
#include "DPGameplayActionLite.generated.h"

class UWorld;
class UDP_GameplayActionComponent;

/**
 * Opaque handle to a granted action instance inside a UDP_GameplayActionComponent.
 *
 * Like FGameplayAbilitySpecHandle, this is a stable id that survives array reordering, so
 * callers can reference a specific granted action without holding a UObject pointer. Two
 * default-constructed handles are invalid and unequal to any generated handle.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_ActionSpecHandle
{
	GENERATED_BODY()

	FDP_ActionSpecHandle() = default;

	/** True if this handle refers to a real granted action. */
	bool IsValid() const { return Id != 0; }

	/** Allocate the next monotonic id (component-local). */
	void GenerateNewId() { static int32 GHandle = 0; Id = ++GHandle; }

	bool operator==(const FDP_ActionSpecHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FDP_ActionSpecHandle& Other) const { return Id != Other.Id; }

	friend uint32 GetTypeHash(const FDP_ActionSpecHandle& H) { return ::GetTypeHash(H.Id); }

	/** Debug string. */
	FString ToString() const { return FString::Printf(TEXT("ActionSpec:%d"), Id); }

private:
	/** 0 = invalid. */
	UPROPERTY()
	int32 Id = 0;
};

/**
 * A granted action: its handle, the live action instance, and runtime cooldown bookkeeping.
 *
 * This is the component's record of "you have this action". The instance is a UPROPERTY
 * TObjectPtr so the component GC-owns it; the cooldown end-time is in world seconds.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_ActionSpec
{
	GENERATED_BODY()

	/** Stable handle identifying this grant. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Action")
	FDP_ActionSpecHandle Handle;

	/** The granted action instance (owned by the component). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Action")
	TObjectPtr<UDP_GameplayActionLite> Action = nullptr;

	/** World time (seconds) at which the cooldown expires. <= now means ready. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Action")
	float CooldownEndTime = 0.f;

	/** True while the action is mid-activation (between Activate and EndAction). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Action")
	bool bIsActive = false;

	FDP_ActionSpec() = default;
};

/**
 * A GAS-FREE lightweight "ability". Blueprintable + Abstract: project subclasses (C++ or BP)
 * implement the activation hooks. The action is a UObject owned by a UDP_GameplayActionComponent
 * (its Outer), so it has a valid world context and is GC-rooted by the component's spec array.
 *
 * Lifecycle: Component grants -> CanActivate gate -> Activate -> (action runs) -> EndAction.
 * Cooldown is enforced by the owning component using world time; the action declares its
 * duration via CooldownDuration and exposes the activation hooks as BlueprintNativeEvents.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class DESIGNPATTERNS_API UDP_GameplayActionLite : public UObject, public IDP_GameplayAction
{
	GENERATED_BODY()

public:
	UDP_GameplayActionLite();

	/** The tag identifying this action. Used for granting, lookup and duplicate detection. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatterns|Action")
	FGameplayTag ActionTag;

	/** Cooldown in seconds applied (from world time) when the action successfully activates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatterns|Action", meta = (ClampMin = "0.0"))
	float CooldownDuration = 0.f;

	/** Tags this action requires the owner to have (any-match disabled; all must be present). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatterns|Action")
	FGameplayTagContainer ActivationRequiredTags;

	/** Tags that, if present on the owner, block activation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatterns|Action")
	FGameplayTagContainer ActivationBlockedTags;

	// ---- Designer-overridable lifecycle (BlueprintNativeEvent) ----

	/**
	 * Gate predicate. Default checks required/blocked tags against the source component. Override
	 * to add costs/resource checks. Cooldown is checked separately by the component (not here).
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Action")
	bool CanActivate(const FDP_ActionActivationData& Data) const;
	virtual bool CanActivate_Implementation(const FDP_ActionActivationData& Data) const;

	/**
	 * Run the action. Default implementation only logs; subclasses do the work. Return true if the
	 * action actually started (the component applies cooldown only on a true return).
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Action")
	bool Activate(const FDP_ActionActivationData& Data);
	virtual bool Activate_Implementation(const FDP_ActionActivationData& Data);

	/**
	 * End the action (cancel or natural completion). Default logs. Override to clean up timers,
	 * montages, spawned effects, etc. bWasCancelled distinguishes cancel from normal completion.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Action")
	void EndAction(const FDP_ActionActivationData& Data, bool bWasCancelled);
	virtual void EndAction_Implementation(const FDP_ActionActivationData& Data, bool bWasCancelled);

	//~ Begin IDP_GameplayAction
	virtual FGameplayTag GetActionTag() const override { return ActionTag; }
	virtual bool CanActivateAction(const FDP_ActionActivationData& Data) const override { return CanActivate(Data); }
	//~ End IDP_GameplayAction

	//~ Begin UObject
	virtual UWorld* GetWorld() const override;
	//~ End UObject

protected:
	/** Resolve the owning component from this action's Outer chain. May be null in the CDO. */
	UDP_GameplayActionComponent* GetOwningComponent() const;
};
