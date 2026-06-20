// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "HUD_InputContextSubsystem.generated.h"

/**
 * Bus payload accompanying a mapped input intent (published under HUDTags::Bus_InputIntent's subtree).
 *
 * Carries the resolved intent tag and the input device class that produced it (so a listener can, e.g.,
 * choose gamepad-vs-mouse confirm behaviour) plus the analog value of the triggering action. Device class
 * is an int32 mirror of EPlat_InputDevice so this struct does not hard-couple the HUD bus payload to the
 * Platform module's enum type; -1 means "unknown / Platform module absent".
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_InputIntentPayload
{
	GENERATED_BODY()

	/** The intent tag that was emitted (also the bus channel this was broadcast on). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	FGameplayTag IntentTag;

	/** Analog magnitude of the triggering action (1.0 for digital buttons). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	float AnalogValue = 0.f;

	/** Input device class that produced this intent (mirrors EPlat_InputDevice; -1 = unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	int32 DeviceClass = -1;
};

class UHUD_InputActionMapDataAsset;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
enum class ETriggerEvent : uint8;
struct FInputActionInstance;

/**
 * Local-player-scoped manager that layers Enhanced-Input mapping contexts by FGameplayTag priority and
 * routes triggered actions to FGameplayTag *intents* on the message bus.
 *
 * Responsibilities:
 *  - Owns the active set of input *layers* (gameplay / menu / dialogue / game-authored). AddContext(tag)
 *    resolves the layer in the active UHUD_InputActionMapDataAsset, loads its UInputMappingContext, adds it
 *    to the player's UEnhancedInputLocalPlayerSubsystem at the layer's priority, and binds that layer's
 *    actions. RemoveContext(tag) tears all of that down. Layers stack: a higher-priority menu context masks
 *    gameplay bindings while open.
 *  - Translates each triggered action into a tagged intent published on HUDTags::Bus_InputIntent's subtree,
 *    so gameplay/menu systems consume intents and never bind to engine input directly.
 *  - Integrates SOFTLY with the Platform input device seam (UPlat_InputRouterSubsystem) — resolved via the
 *    service-locator-style world lookup, used only to tag emitted intents with the current device class; a
 *    missing Platform module degrades gracefully (intents still publish, just without a device hint).
 *
 * Purely local input plumbing: never replicated. The intents it publishes are the boundary; whether an
 * intent results in a server RPC is the consuming gameplay system's concern (via a player-owned component).
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_InputContextSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Replace the active action map (the source of layer definitions + action->intent bindings). Removes any
	 * currently-active layers first (they are re-resolvable against the new map). Loads synchronously.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Input")
	void SetActionMap(UHUD_InputActionMapDataAsset* InMap);

	/** The active action map, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Input")
	UHUD_InputActionMapDataAsset* GetActionMap() const { return ActionMap; }

	/**
	 * Activate the input layer LayerTag: add its mapping context (at its configured priority) and bind its
	 * action->intent routes. Idempotent — re-adding an active layer is a no-op. Returns false if the layer
	 * is unknown or the player's Enhanced-Input subsystem is unavailable.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Input")
	bool AddContext(FGameplayTag LayerTag);

	/**
	 * Deactivate the input layer LayerTag: remove its mapping context and unbind its action routes. Safe to
	 * call for an inactive/unknown layer (no-op). Returns true if a layer was actually removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Input")
	bool RemoveContext(FGameplayTag LayerTag);

	/** True if LayerTag is currently active. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Input")
	bool IsContextActive(FGameplayTag LayerTag) const { return ActiveLayers.Contains(LayerTag); }

	/** All currently-active layer tags. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Input")
	void GetActiveContexts(TArray<FGameplayTag>& OutLayers) const { ActiveLayers.GetKeys(OutLayers); }

private:
	/** Per-active-layer bookkeeping so RemoveContext can tear down exactly what AddContext set up. */
	struct FActiveLayer
	{
		/** The loaded mapping context added to the Enhanced-Input subsystem. */
		TWeakObjectPtr<UInputMappingContext> MappingContext;

		/** Binding handles created on the Enhanced-Input component for this layer's actions. */
		TArray<uint32> BindingHandles;
	};

	/** Resolve the player's Enhanced-Input local-player subsystem (null if unavailable). */
	UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem() const;

	/** Resolve the player's Enhanced-Input component off its player controller (null if unavailable). */
	UEnhancedInputComponent* GetEnhancedInputComponent() const;

	/** Apply the developer-settings default action map + default active layers on initialize. */
	void ApplyDefaultsFromSettings();

	/** Bind one action->intent route on the Enhanced-Input component, returning the engine binding handle. */
	uint32 BindActionRoute(UEnhancedInputComponent* InputComp, UInputAction* Action,
		ETriggerEvent TriggerEvent, FGameplayTag IntentTag);

	/** Enhanced-Input callback: translate a triggered action into a bus intent. */
	void HandleActionTriggered(const FInputActionInstance& Instance, FGameplayTag IntentTag);

	/**
	 * Publish IntentTag on the message bus with an FHUD_InputIntentPayload, tagging it with the analog value
	 * and (softly) the current input device class.
	 */
	void PublishIntent(FGameplayTag IntentTag, float AnalogValue);

	/** Resolve the current input device class as an int32 (mirrors EPlat_InputDevice), or -1 if unknown. */
	int32 ResolveCurrentDeviceClass() const;

	/** The active action map (layer + binding definitions). Owning ref so it is GC-kept while bound. */
	UPROPERTY()
	TObjectPtr<UHUD_InputActionMapDataAsset> ActionMap = nullptr;

	/** Active layers keyed by tag, each tracking its mapping context + binding handles for teardown. */
	TMap<FGameplayTag, FActiveLayer> ActiveLayers;
};
