// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "USurv_NeedsComponent.generated.h"

/** The survival meters tracked by USurv_NeedsComponent. */
UENUM(BlueprintType)
enum class ESurv_NeedType : uint8
{
	Hunger      UMETA(DisplayName = "Hunger"),
	Thirst      UMETA(DisplayName = "Thirst"),
	Stamina     UMETA(DisplayName = "Stamina"),
	Temperature UMETA(DisplayName = "Temperature")
};

/** Tuning + replicated current value for a single need meter. */
USTRUCT(BlueprintType)
struct FSurv_NeedMeter
{
	GENERATED_BODY()

	/** Which meter this is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Needs")
	ESurv_NeedType Type = ESurv_NeedType::Hunger;

	/** Current value, 0..Max. Replicated as part of the component's Meters array. */
	UPROPERTY(BlueprintReadOnly, Category = "Survival|Needs")
	float Current = 100.f;

	/** Maximum value the meter can hold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Needs", meta = (ClampMin = "1.0"))
	float Max = 100.f;

	/** Units drained per second by the authority tick. Negative values regenerate the meter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Needs")
	float DrainPerSecond = 1.f;

	/** At/below this value the meter is "critical" and fires OnNeedCritical + contributes damage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Needs", meta = (ClampMin = "0.0"))
	float CriticalThreshold = 10.f;

	/** True while Current <= CriticalThreshold; used to fire OnNeedCritical only on the edge. */
	UPROPERTY()
	bool bWasCritical = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSurv_OnNeedChanged, ESurv_NeedType, Need, float, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnNeedCritical, ESurv_NeedType, Need);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSurv_OnNeedsDamage, float, DamageAmount, FGameplayTag, SourceNeedTag);

/**
 * Replicated survival-needs component: hunger, thirst, stamina, temperature.
 *
 * The authority ticks each meter's drain and replicates the values; clients read them for UI.
 * Crossing a meter's CriticalThreshold fires OnNeedCritical (edge-triggered). While any meter is
 * critical the component applies periodic damage by (a) calling ISurv_HealthSinkInterface on the
 * owner if it implements it — the SOFT seam to a Combat health system — and (b) broadcasting
 * OnNeedsDamage so projects without that interface can still react. No hard Combat dependency.
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API USurv_NeedsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USurv_NeedsComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/** Current value of a meter (0 if not configured). Safe on clients (replicated). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Needs")
	float GetNeed(ESurv_NeedType Type) const;

	/** Normalized 0..1 value of a meter. Safe on clients. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Needs")
	float GetNeedNormalized(ESurv_NeedType Type) const;

	/** True if the meter is at/below its critical threshold. Safe on clients. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Needs")
	bool IsNeedCritical(ESurv_NeedType Type) const;

	/**
	 * Restore (or, if Delta is negative, reduce) a meter by Delta, clamped to [0, Max].
	 * AUTHORITY-ONLY: no-op on clients. Use for eating/drinking/resting.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Needs")
	void ApplyNeedDelta(ESurv_NeedType Type, float Delta);

	/** Fired whenever a meter value changes (server on tick, clients on replication). */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Needs")
	FSurv_OnNeedChanged OnNeedChanged;

	/** Fired (edge-triggered) when a meter drops to/below its critical threshold. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Needs")
	FSurv_OnNeedCritical OnNeedCritical;

	/** Fired on the authority when critical needs inflict damage (mirrors the health-sink call). */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Needs")
	FSurv_OnNeedsDamage OnNeedsDamage;

	/** Damage applied per critical meter, per damage interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Needs", meta = (ClampMin = "0.0"))
	float CriticalDamagePerTick = 2.f;

	/** Seconds between critical-damage applications. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Needs", meta = (ClampMin = "0.1"))
	float CriticalDamageInterval = 1.f;

protected:
	/** All configured meters. Replicated so clients can drive needs UI. */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Meters, Category = "Survival|Needs")
	TArray<FSurv_NeedMeter> Meters;

	/** Accumulated time toward the next critical-damage application (server only). */
	float DamageAccumulator = 0.f;

	UFUNCTION()
	void OnRep_Meters();

	/** True on the network authority. Gate every mutator on this. */
	bool HasAuthorityToMutate() const;

	/** Find a meter by type (mutable / const). */
	FSurv_NeedMeter* FindMeter(ESurv_NeedType Type);
	const FSurv_NeedMeter* FindMeter(ESurv_NeedType Type) const;

	/** Map a need type to its native gameplay tag (Surv.Need.<X>) for the health-sink call. */
	static FGameplayTag NeedToTag(ESurv_NeedType Type);

	/** Apply DamageAmount via the soft health-sink interface (if present) and broadcast OnNeedsDamage. */
	void ApplyCriticalDamage(float DamageAmount, ESurv_NeedType SourceNeed);
};
