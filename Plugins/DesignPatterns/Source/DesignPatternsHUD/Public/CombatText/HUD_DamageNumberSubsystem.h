// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "HUD_DamageNumberSubsystem.generated.h"

class UHUD_DamageNumberViewModel;
class UHUD_DamageNumberStyleDataAsset;
struct FHUD_FloatingTextView;

/**
 * Local-player-scoped floating combat-text ("damage numbers") presenter.
 *
 * Listens on the combat hit/damage bus channels (DP.Bus.Combat.HitFeedback + DP.Bus.Combat.Damaged) via
 * UDP_MessageBusSubsystem::ListenNative(ExactOrChild, OwnerForLifetime = this), reads each payload's victim
 * actor / damage amount / impact point / classification tag GENERICALLY by reflection (field names tunable
 * on the style asset, defaults matching the shipped FCombat_HitResult) so NO Combat header is included.
 *
 * It maintains a fixed-capacity pool of active floating-text items (cap + lifetime from the style asset),
 * projects each item's world impact point to screen space through the owning PlayerController, classifies
 * crit / heal / weakpoint by tag, resolves per-classification style, and advances + recycles items on an
 * FTSTicker. Each tick it pushes the current set into UHUD_DamageNumberViewModel; the bound UDP_ViewBase
 * renders the array. Purely local/cosmetic: it is driven by already-replicated combat events re-broadcast
 * locally, never replicates, and never touches authoritative state.
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_DamageNumberSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Replace the active style/tuning asset (re-clamps the pool to the new cap). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|DamageNumber")
	void SetStyleAsset(UHUD_DamageNumberStyleDataAsset* InStyle);

	/** The ViewModel the floating-text UI binds to (never null after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|DamageNumber")
	UHUD_DamageNumberViewModel* GetViewModel() const { return ViewModel; }

	/**
	 * Spawn a floating number directly (the bus path funnels here). WorldLocation is projected to screen each
	 * tick; Classification selects crit/heal/weakpoint styling; Victim is currently informational (reserved
	 * for future per-victim stacking). No-op when no style asset is set or the pool/PC is unavailable.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|DamageNumber")
	void SpawnNumber(const FVector& WorldLocation, float Amount, FGameplayTag Classification, AActor* Victim);

	/** Append one debug line per active item (backing for a debug dump command). */
	void DumpTo(TArray<FString>& OutLines) const;

private:
	/** One pooled, active floating number (world anchor + timing + resolved style snapshot). */
	struct FActiveNumber
	{
		int64 InstanceId = 0;
		FVector WorldLocation = FVector::ZeroVector;
		FVector2D JitterPixels = FVector2D::ZeroVector;
		float Amount = 0.f;
		FGameplayTag Classification;
		double SpawnTimeSeconds = 0.0;
	};

	/** Combat-bus handlers: extract victim/amount/impact/classification by reflection, then SpawnNumber. */
	void HandleCombatEvent(const FDP_Message& Message);

	/** Per-frame: advance lifetimes, recycle expired, reproject survivors to screen, push to the ViewModel. */
	bool TickNumbers(float DeltaTime);

	/** Project a world point to viewport pixels via the owning PC; returns false if behind camera / no PC. */
	bool ProjectToScreen(const FVector& World, FVector2D& OutScreen) const;

	/** The owning local player's current PlayerController (null if not yet possessed). */
	APlayerController* GetOwningPlayerController() const;

	// --- Reflection payload readers (generic; no Combat header) ---

	/** Read an AActor* / TObjectPtr / TWeakObjectPtr<AActor> field from a bus payload by name (null if absent). */
	static AActor* ReadActorField(const FInstancedStruct& Payload, FName FieldName);

	/** Read a float / double field from a bus payload by name into OutValue (false if absent / wrong type). */
	static bool ReadFloatField(const FInstancedStruct& Payload, FName FieldName, float& OutValue);

	/** Read an FVector field from a bus payload by name into OutValue (false if absent / wrong type). */
	static bool ReadVectorField(const FInstancedStruct& Payload, FName FieldName, FVector& OutValue);

	/** Read an FGameplayTag field from a bus payload by name into OutValue (false if absent / wrong type). */
	static bool ReadTagField(const FInstancedStruct& Payload, FName FieldName, FGameplayTag& OutValue);

	/** The pure-projection ViewModel (owned, GC-kept). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_DamageNumberViewModel> ViewModel = nullptr;

	/** The active style/tuning asset (owned ref so it is GC-kept while bound). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_DamageNumberStyleDataAsset> Style = nullptr;

	/** The active pool of floating numbers (capped by Style->MaxConcurrent). */
	TArray<FActiveNumber> ActiveNumbers;

	/** FTSTicker handle driving TickNumbers; removed in Deinitialize. */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Monotonic instance-id source. */
	int64 NextInstanceId = 1;
};
