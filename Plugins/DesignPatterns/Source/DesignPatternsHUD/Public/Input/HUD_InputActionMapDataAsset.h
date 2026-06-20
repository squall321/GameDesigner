// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "HUD_InputActionMapDataAsset.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Binds one Enhanced-Input UInputAction to the FGameplayTag *intent* it publishes on the message bus.
 *
 * The HUD input layer never wires actions to gameplay directly; it translates a triggered action into a
 * tagged intent (e.g. DP.Bus.HUD.Input.Jump) that gameplay/menu systems listen for. Keeping action->intent
 * data-driven means a project can re-key inputs without recompiling.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_ActionIntentBinding
{
	GENERATED_BODY()

	/** The Enhanced-Input action that, when triggered, emits the intent below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	TObjectPtr<UInputAction> Action = nullptr;

	/**
	 * Intent tag broadcast (relative to / under HUDTags::Bus_InputIntent) when Action triggers. A child of
	 * DP.Bus.HUD.Input is recommended so all input intents share one bus subtree.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	FGameplayTag IntentTag;

	/**
	 * When true the intent is only emitted on the action's "Started" trigger edge (button down); when
	 * false it is emitted on every "Triggered" event (held / continuous). Designer choice per binding.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	bool bOnStartedOnly = true;
};

/**
 * One input *context layer*: an Enhanced-Input mapping context plus the layer tag + priority the HUD input
 * context subsystem adds it at, and the action->intent bindings that context contributes.
 *
 * The mapping context is a soft ref so the data asset itself stays lightweight and contexts load on demand
 * when a layer is first added. Priority is the Enhanced-Input mapping-context priority (higher wins), so
 * a menu layer can mask gameplay bindings while it is active.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_InputContextLayer
{
	GENERATED_BODY()

	/**
	 * Layer identity (e.g. HUDTags::InputLayer_Menu). Add/RemoveContext on the subsystem is keyed by this
	 * tag, and the subsystem layers contexts by Priority below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	FGameplayTag LayerTag;

	/** The Enhanced-Input mapping context this layer adds (soft — loaded when the layer is added). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	TSoftObjectPtr<UInputMappingContext> MappingContext;

	/** Enhanced-Input mapping-context priority for this layer (higher masks lower). Designer tunable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	int32 Priority = 0;

	/** Action -> intent bindings this context contributes while active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	TArray<FHUD_ActionIntentBinding> Bindings;
};

/**
 * Data asset that fully describes a game's HUD input layering: the set of context layers (gameplay / menu /
 * dialogue / game-authored), each with its mapping context, priority, and action->intent bindings.
 *
 * UHUD_InputContextSubsystem reads this to (a) know which UInputMappingContext to add when a layer tag is
 * requested, at what priority, and (b) translate triggered actions into bus intents. Being a UDP_DataAsset
 * it is tag-identified and registry-discoverable; everything here is opaque tags + soft refs so no gameplay
 * type is referenced and inputs are fully re-keyable in content.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Input Action Map"))
class DESIGNPATTERNSHUD_API UHUD_InputActionMapDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The context layers this map defines, in no particular order (priority lives per-layer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Input")
	TArray<FHUD_InputContextLayer> Layers;

	/**
	 * Find the layer definition for LayerTag, or null if this map does not define it. Native-only (returns a
	 * raw struct pointer, which UHT does not allow as a UFUNCTION return) — consumed by the input subsystem.
	 */
	const FHUD_InputContextLayer* FindLayer(FGameplayTag LayerTag) const;

	//~ Begin UDP_DataAsset
	/** Groups all HUD input maps under one asset-manager type bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
