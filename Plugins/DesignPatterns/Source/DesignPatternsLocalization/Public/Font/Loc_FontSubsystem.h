// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "Fonts/SlateFontInfo.h"
#include "Loc/Seam_FontProfileProvider.h"
#include "Loc_FontSubsystem.generated.h"

class ULoc_FontProfileDataAsset;

/**
 * GameInstance-scoped font / glyph-fallback authority. The ONLY place FSlateFontInfo is composed.
 *
 * RESPONSIBILITIES:
 *  - Binds ULoc_LocalizationSubsystem::OnCultureChanged and selects the ULoc_FontProfileDataAsset whose
 *    Culture matches the active culture (exact, then language prefix), discovered via the asset manager.
 *  - Composes an FSlateFontInfo for a UI font-role tag from the selected profile (role override > default),
 *    loading the soft font face on demand and applying the profile's glyph-fallback chain.
 *  - Exposes the current culture's RTL flag for UI horizontal-layout flipping.
 *  - Implements ISeam_FontProfileProvider so UI resolves soft font faces + RTL across the SLATE-FREE seam
 *    (the FSlateFontInfo never crosses the seam — only soft refs + bool do). Publishes itself under
 *    DPLocTags::Service_FontProfile (weak-observed).
 *
 * LOCAL / per-machine — font selection is presentation; nothing replicates. GC: GameInstance subsystem;
 * the selected profile is held with an owning TObjectPtr while active; the culture binding is removed on
 * Deinitialize.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_FontSubsystem : public UDP_GameInstanceSubsystem, public ISeam_FontProfileProvider
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Compose an FSlateFontInfo for FontRole from the active culture's font profile. Resolves the role
	 * override (or the profile default), loads the soft font face synchronously (cached), applies the
	 * fallback chain, and sets the size. Returns an engine-default FSlateFontInfo when no profile/face is
	 * available (documented inert fallback) so UI always gets a usable font.
	 *
	 * This is the ONLY API that exposes FSlateFontInfo; it is concrete (not on the seam).
	 */
	UFUNCTION(BlueprintPure, Category = "Localization|Font")
	FSlateFontInfo GetCultureFont(FGameplayTag FontRole) const;

	/** Whether the active culture's profile is right-to-left (false when no profile selected). */
	UFUNCTION(BlueprintPure, Category = "Localization|Font")
	bool IsCurrentCultureRTL() const;

	/** The active culture's glyph-fallback faces as soft refs (mirrors the seam read; empty when none). */
	UFUNCTION(BlueprintPure, Category = "Localization|Font")
	TArray<TSoftObjectPtr<UObject>> GetFallbackFontFaces_Native() const;

	/** The culture code of the currently selected font profile, or empty. */
	UFUNCTION(BlueprintPure, Category = "Localization|Font")
	FString GetActiveProfileCulture() const;

	//~ Begin ISeam_FontProfileProvider (SLATE-FREE — soft refs + bool only)
	virtual TSoftObjectPtr<UObject> GetCultureFontFace_Implementation(FGameplayTag Role) const override;
	virtual TArray<TSoftObjectPtr<UObject>> GetFallbackFontFaces_Implementation() const override;
	virtual bool IsRightToLeft_Implementation() const override;
	//~ End ISeam_FontProfileProvider

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Handler bound to ULoc_LocalizationSubsystem::OnCultureChanged: reselects the profile. */
	UFUNCTION()
	void HandleCultureChanged(const FString& NewCulture);

	/** Discover all font profiles via the asset manager and pick the one matching CultureCode (exact>prefix). */
	void SelectProfileForCulture(const FString& CultureCode);

	/** Bind/unbind the culture-changed delegate (null-safe). */
	void BindCultureChanged();
	void UnbindCultureChanged();

	/** Publish/unpublish under DPLocTags::Service_FontProfile (weak-observed). */
	void PublishToServiceLocator();
	void UnpublishFromServiceLocator();

	/** Resolve the soft font-face for Role from the active profile (override>default); empty if none. */
	TSoftObjectPtr<UObject> ResolveFaceForRole(FGameplayTag Role, FName& OutTypeface, int32& OutSize) const;

	/** The currently selected per-culture font profile (owning ref while selected). */
	UPROPERTY(Transient)
	TObjectPtr<ULoc_FontProfileDataAsset> ActiveProfile = nullptr;

	/** True once Initialize bound the culture delegate. */
	bool bCultureBound = false;
};
