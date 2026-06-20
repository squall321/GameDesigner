// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Loc_LocalizationSubsystem.generated.h"

class ULoc_DeveloperSettings;
struct FLoc_StringTableBinding;

/**
 * Broadcast whenever the active culture changes (via SetCurrentCulture or an external engine change the
 * subsystem detects). Carries the new culture code so listeners can re-pull localized text. Dynamic so
 * Blueprint and other modules can bind without a hard dependency on this class.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLoc_OnCultureChanged, const FString&, NewCulture);

/**
 * GameInstance-scoped localization facade over the engine's FInternationalization + FStringTableRegistry.
 *
 * This subsystem NEVER reinvents FText. It is a thin, tag-friendly wrapper that:
 *  - reads/sets the active culture through FInternationalization::Get().GetCurrentCulture() /
 *    SetCurrentCulture(), optionally persisting the choice via the engine localization config;
 *  - enumerates the cultures the engine knows about (GetAvailableCultures);
 *  - resolves designer-authored FGameplayTag keys to FText via FText::FromStringTable, using the
 *    string-table bindings configured in ULoc_DeveloperSettings (so callers never touch table ids);
 *  - fires OnCultureChanged so subtitle UI, HUD and any other presentation re-pulls its text.
 *
 * It publishes itself into the service locator under DPLocTags::Service_Localization (weak-observed) so
 * other modules can resolve culture/text without hard-including this module. Player-local: nothing here
 * replicates. Independently removable — when this subsystem is absent, callers simply use raw FText.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_LocalizationSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Fired after the active culture changes; carries the new culture code (e.g. "fr-FR"). */
	UPROPERTY(BlueprintAssignable, Category = "Localization")
	FLoc_OnCultureChanged OnCultureChanged;

	/**
	 * The currently active culture code (FInternationalization::GetCurrentCulture()->GetName()).
	 * Returns an empty string only if internationalization is somehow unavailable.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Localization")
	FString GetCurrentCulture() const;

	/**
	 * Request a culture change. Wraps FInternationalization::Get().SetCurrentCulture(CultureCode); on
	 * success, optionally persists per settings and broadcasts OnCultureChanged. A no-op (returns true)
	 * if CultureCode already matches the active culture.
	 *
	 * @param CultureCode Culture/locale code (e.g. "en", "fr-FR"). Empty is rejected (returns false).
	 * @return true if the culture is now CultureCode (changed or already active); false if the engine
	 *         rejected it (e.g. not an available culture).
	 */
	UFUNCTION(BlueprintCallable, Category = "Localization")
	bool SetCurrentCulture(const FString& CultureCode);

	/**
	 * Enumerate the culture codes the engine knows about (localized + game cultures), de-duplicated.
	 * Wraps FInternationalization::Get().GetAvailableCultures(). Useful to populate a language menu.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Localization")
	TArray<FString> GetAvailableCultures() const;

	/**
	 * Convenience text lookup: resolve a designer-authored FGameplayTag Key to FText by finding the
	 * configured string-table binding whose NamespaceRoot is Key or an ancestor of Key, then calling
	 * FText::FromStringTable(TableId, LeafKeyString). The leaf key is the dot-joined remainder of Key
	 * below the binding's NamespaceRoot.
	 *
	 * @param Key       The localization key tag (e.g. DP.Loc.UI.Confirm).
	 * @param bFound    Set true if a binding matched and the table row resolved; false otherwise.
	 * @return The localized FText, or FText::GetEmpty() when no binding matches. FText::FromStringTable
	 *         itself returns a visible "missing entry" marker for an unknown row, which the engine handles.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Localization")
	FText FindText(FGameplayTag Key, bool& bFound) const;

	/** Blueprint/native-friendly overload of FindText that discards the found flag. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Localization", meta = (DisplayName = "Find Text (Simple)"))
	FText FindTextSimple(FGameplayTag Key) const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Apply the configured DefaultCulture (if any) on Initialize. Logged; failures are non-fatal. */
	void ApplyDefaultCulture();

	/** Publish this subsystem into the service locator (weak-observed) under Service_Localization. */
	void PublishToServiceLocator();

	/** Remove the service-locator registration on Deinitialize. */
	void UnpublishFromServiceLocator();

	/**
	 * Find the string-table binding whose NamespaceRoot matches Key (exact or ancestor), preferring the
	 * most specific (deepest) NamespaceRoot when several match. Returns null if none match.
	 * On success OutLeafKey is the dot-joined remainder of Key below NamespaceRoot (or the full key if
	 * NamespaceRoot equals Key — empty leaf, in which case the binding's own leaf naming is used).
	 */
	const FLoc_StringTableBinding* FindBindingForKey(const FGameplayTag& Key, FString& OutLeafKey) const;

	/** Force-load + register a binding's string-table asset if it is not yet registered. */
	void EnsureTableRegistered(const FLoc_StringTableBinding& Binding) const;

	/**
	 * The culture code last applied by this subsystem, cached so GetDPDebugString and change-detection do
	 * not have to round-trip through FInternationalization. Updated on every successful SetCurrentCulture.
	 */
	FString CachedCulture;
};
