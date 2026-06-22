// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/Texture2D.h"
#include "Data/DPDataAsset.h"
#include "UPlat_InputGlyphTypes.generated.h"

/**
 * Input-presentation FAMILY for glyph selection. This is an additive REFINEMENT of the coarse
 * EPlat_InputDevice (KeyboardMouse / Gamepad / Touch) — it splits Gamepad into vendor-specific
 * families so the right button art shows. It neither edits nor replaces EPlat_InputDevice; the glyph
 * subsystem maps from device class to family (refining Gamepad behind platform #ifdefs).
 */
UENUM(BlueprintType)
enum class EPlat_InputFamily : uint8
{
	/** Unknown / generic gamepad (used as the safe fallback). */
	Generic			UMETA(DisplayName = "Generic"),
	/** Xbox-style controller. */
	Xbox			UMETA(DisplayName = "Xbox"),
	/** PlayStation-style controller. */
	PlayStation		UMETA(DisplayName = "PlayStation"),
	/** Nintendo-style controller. */
	Nintendo		UMETA(DisplayName = "Nintendo"),
	/** Steam Deck built-in controls. */
	SteamDeck		UMETA(DisplayName = "Steam Deck"),
	/** Touch screen. */
	Touch			UMETA(DisplayName = "Touch"),
	/** Keyboard + mouse. */
	KeyboardMouse	UMETA(DisplayName = "Keyboard & Mouse")
};

/**
 * A resolved glyph for one action on the active family: a soft texture (so the UI controls loading)
 * plus a localized label. Plain BlueprintReadOnly value type.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPLATFORM_API FPlat_InputGlyph
{
	GENERATED_BODY()

	/** Soft pointer to the glyph texture (may be null if the family relies on the label alone). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Platform|Input")
	TSoftObjectPtr<UTexture2D> GlyphTexture;

	/** Localized human-readable label for the binding (e.g. "Space", "Ⓐ", "✕"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Platform|Input")
	FText Label;

	/** The action this glyph represents (e.g. DP.Input.Action.Jump). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Platform|Input")
	FGameplayTag ActionTag;
};

/**
 * Per-family glyph bank mapping action tags to glyphs, registered through the core data registry by
 * its inherited DataTag. A project ships one bank per family (Xbox, PlayStation, KeyboardMouse, ...).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSPLATFORM_API UPlat_GlyphSet : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The input family this bank's glyphs depict. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Input")
	EPlat_InputFamily Family = EPlat_InputFamily::Generic;

	/** Action tag -> glyph for this family. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Input")
	TMap<FGameplayTag, FPlat_InputGlyph> Glyphs;

	/**
	 * Resolve the glyph for an action tag in this bank.
	 * @return True if a glyph was found and copied into Out.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Input")
	bool ResolveGlyph(FGameplayTag ActionTag, FPlat_InputGlyph& Out) const;

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
