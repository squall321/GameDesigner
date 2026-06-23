// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "Loc_FontProfileDataAsset.generated.h"

/**
 * One per-UI-role font override within a culture's font profile: the role tag (DP.Loc.Font.Body, ...) and
 * the soft font-face object to use for it. SOFT ref only so the profile asset never force-loads a font
 * atlas; the font subsystem loads on demand when composing an FSlateFontInfo.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_FontRoleOverride
{
	GENERATED_BODY()

	/** UI role this override applies to (body / heading / subtitle / a project-defined role). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Font")
	FGameplayTag Role;

	/**
	 * Soft reference to the font-face object (a UFontFace or a UFont) for this role. Generic UObject soft
	 * ref so the asset can point at either type without this module hard-typing it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Font")
	TSoftObjectPtr<UObject> FontFace;

	/**
	 * Typeface name within the font face to request (e.g. "Regular", "Bold"). Empty uses the engine's
	 * default typeface for the face.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Font")
	FName TypefaceName;

	/** Point size for this role. Clamped >0; a profile with 0 falls back to the bank default size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Font", meta = (ClampMin = "1"))
	int32 SizePoints = 18;
};

/**
 * A per-culture font profile: the default body font face, an ordered glyph-fallback chain (for CJK /
 * Cyrillic / emoji coverage), the RTL flag, and per-role overrides. The font subsystem selects the profile
 * whose Culture matches the active culture and exposes resolved FSlateFontInfo / soft fallback chain / RTL.
 *
 * Identity is the inherited DataTag. Data-driven: no font names or sizes are hardcoded in code.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Localization Font Profile"))
class DESIGNPATTERNSLOCALIZATION_API ULoc_FontProfileDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Culture code this profile applies to (e.g. "ja", "ar"). Matched exactly, then by language prefix. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Font")
	FString Culture;

	/** The default font face for body text when a role has no explicit override. SOFT ref. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Font")
	TSoftObjectPtr<UObject> DefaultFontFace;

	/** Default typeface name within DefaultFontFace ("Regular" if empty). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Font")
	FName DefaultTypefaceName;

	/** Default point size for the body role when not overridden. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Font", meta = (ClampMin = "1"))
	int32 DefaultSizePoints = 18;

	/** Ordered glyph-fallback faces (highest priority first) consulted when the primary lacks a glyph. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Font")
	TArray<TSoftObjectPtr<UObject>> FallbackFontFaces;

	/** Whether this culture's script lays out right-to-left. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Font")
	bool bRightToLeft = false;

	/** Per-role overrides (body/heading/subtitle/...). A role not listed uses the default face/size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Font")
	TArray<FLoc_FontRoleOverride> RoleOverrides;

	/** This profile's culture code. */
	UFUNCTION(BlueprintPure, Category = "Localization|Font")
	FString GetCulture() const { return Culture; }

	/** Whether this profile is right-to-left. */
	UFUNCTION(BlueprintPure, Category = "Localization|Font")
	bool IsRTL() const { return bRightToLeft; }

	/**
	 * Find the role override for Role, or null if none (caller uses the default face/size). Hierarchy-aware:
	 * an override on DP.Loc.Font picks up DP.Loc.Font.Body if no exact override exists, preferring the most
	 * specific match.
	 */
	const FLoc_FontRoleOverride* FindRoleOverride(FGameplayTag Role) const;

	//~ Begin UDP_DataAsset
	/** Collapse all font profiles into one asset-manager bucket so a project can scan them as a family. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject
	/** Flags an empty Culture or a missing default font face. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
