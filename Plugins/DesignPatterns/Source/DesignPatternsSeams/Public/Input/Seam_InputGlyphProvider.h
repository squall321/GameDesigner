// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Engine/Texture2D.h"
#include "Seam_InputGlyphProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_InputGlyphProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Input-glyph seam, owned by the Platform module's glyph subsystem (UPlat_InputGlyphSubsystem).
 * UI widgets resolve the button icon + localized label for a gameplay action on the player's
 * CURRENT input device (keyboard/mouse, a vendor-specific gamepad family, touch) WITHOUT depending
 * on the Platform module: they resolve the provider from the service locator under
 * DP.Service.Platform.Glyphs and wrap it in a TScriptInterface<ISeam_InputGlyphProvider>.
 *
 * The glyph texture crosses the seam as a TSoftObjectPtr so the UI controls loading and so the
 * Seams module never force-loads a glyph atlas. No Platform-module type leaks across the seam.
 * Sits beside Seam_InputModeArbiter.h. Held weakly and a no-op when unset.
 */
class DESIGNPATTERNSSEAMS_API ISeam_InputGlyphProvider
{
	GENERATED_BODY()

public:
	/**
	 * Resolve the glyph for an action tag on the active input family.
	 *
	 * @param ActionTag    Identity of the action (e.g. DP.Input.Action.Jump).
	 * @param OutTexture   Soft pointer to the glyph texture for the active family (may be null if
	 *                     unmapped; the caller decides whether to fall back to the label).
	 * @param OutLabel     Localized human-readable label for the binding (e.g. "Space", "Ⓐ").
	 * @return True if a glyph and/or label was resolved for the action on the active family.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Input")
	bool ResolveActionGlyph(FGameplayTag ActionTag, TSoftObjectPtr<UTexture2D>& OutTexture, FText& OutLabel) const;
};
