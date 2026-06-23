// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "Seam_FontProfileProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_FontProfileProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * READ seam exposing the active culture's font profile to UI WITHOUT pulling Slate into the Seams module.
 *
 * The Localization module owns the concrete implementer (ULoc_FontSubsystem) and publishes a
 * TScriptInterface<ISeam_FontProfileProvider> under a service-locator key owned by Localization
 * (DP.Service.Loc.FontProfile, weak-observed). UI code resolves it weakly to learn which font face the
 * current culture wants for a given UI role and whether the script is right-to-left, then composes its own
 * FSlateFontInfo locally.
 *
 * SLATE-FREE INVARIANT (load-bearing): the Seams module is a LEAF — it depends on nothing but the engine
 * core (Core/CoreUObject/GameplayTags + the StructUtils version shim). Adding SlateCore here to surface an
 * FSlateFontInfo would break that invariant and create a dependency cycle. Therefore ONLY font-face SOFT
 * references (TSoftObjectPtr<UObject>, which the consumer loads + interprets as a UFontFace/UFont) and a
 * bool RTL flag cross this seam. The actual FSlateFontInfo is composed inside Localization's concrete
 * ULoc_FontSubsystem (which depends on Slate) and never appears in this header.
 *
 * House style: BlueprintNativeEvent UINTERFACE so it is resolvable as a TScriptInterface and a project may
 * implement it in Blueprint. The shipped INERT default returns empty soft refs + LTR, so a project without
 * a font subsystem gets the engine's default font and left-to-right layout.
 *
 * THREADING / AUTHORITY: font selection is player-local cosmetic state; nothing replicates. All reads are
 * const and happen on the game thread.
 */
class DESIGNPATTERNSSEAMS_API ISeam_FontProfileProvider
{
	GENERATED_BODY()

public:
	/**
	 * Resolve the soft font-face reference the active culture wants for a UI role (e.g. DP.Loc.Font.Body,
	 * DP.Loc.Font.Heading). The consumer loads the soft object and builds an FSlateFontInfo from it. An
	 * empty/unset result means "use the engine default font for this role".
	 *
	 * @param Role A UI font-role tag the project defines (body, heading, numeric, ...).
	 * @return Soft reference to the font face object (a UFontFace or UFont); empty when unmapped.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Font")
	TSoftObjectPtr<UObject> GetCultureFontFace(FGameplayTag Role) const;

	/**
	 * The ordered glyph-fallback chain for the active culture: font faces consulted, in order, when the
	 * primary face lacks a glyph (e.g. CJK / Cyrillic / emoji coverage). May be empty (no fallbacks).
	 *
	 * @return Soft references to fallback font-face objects, highest priority first.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Font")
	TArray<TSoftObjectPtr<UObject>> GetFallbackFontFaces() const;

	/**
	 * Whether the active culture's script is right-to-left (Arabic, Hebrew, ...). UI uses this to flip
	 * horizontal layout / text justification. The font subsystem derives it from the culture's font profile.
	 *
	 * @return true when the active culture should lay out right-to-left.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Font")
	bool IsRightToLeft() const;
};
