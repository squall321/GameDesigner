// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Loc_LocalizationQASubsystem.generated.h"

/**
 * Per-culture QA result for one resolved string-table key. A "missing" key is one that either had
 * no configured table binding, or whose table row did not exist in the engine's string registry for
 * the requested culture. An "unlocalized" key resolved to a row but the row's value is
 * culture-invariant (identical to the source language), suggesting the translator left it blank.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_QAKeyResult
{
	GENERATED_BODY()

	/** The key tag that was checked. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|QA")
	FGameplayTag Key;

	/** The culture code under which the check was performed (e.g. "fr-FR"). */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|QA")
	FString Culture;

	/** Whether the key's configured string-table binding was found and its row resolved. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|QA")
	bool bResolved = false;

	/**
	 * True when the resolved FText is culture-invariant: it carries the source-language literal rather
	 * than a string-table entry (suggests the translator left it untranslated for this culture). Only
	 * meaningful when bResolved is true.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|QA")
	bool bCultureInvariant = false;

	/** The resolved text (for reference in tooling / log output). Empty when bResolved is false. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|QA")
	FText ResolvedText;
};

/**
 * Aggregated QA scan result for one or more cultures. Contains only the FAILING entries (missing or
 * unlocalized), so an empty report is an all-green result. A QA tool or editor utility can present
 * this to the translator or flag it in CI.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_QAReport
{
	GENERATED_BODY()

	/** Cultures that were scanned (in the order they were requested). */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|QA")
	TArray<FString> ScannedCultures;

	/**
	 * Keys that failed to resolve (no binding or missing row) in at least one scanned culture. Each
	 * entry carries the specific culture so a report with multiple cultures is still per-entry precise.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|QA")
	TArray<FLoc_QAKeyResult> MissingKeys;

	/**
	 * Keys that resolved but whose text is culture-invariant in at least one scanned culture — likely
	 * untranslated source-fallback rows.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|QA")
	TArray<FLoc_QAKeyResult> UnlocalizedKeys;

	/** Total number of key-culture pairs checked (scanned cultures × distinct keys). */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|QA")
	int32 TotalChecked = 0;

	/** Convenience: true when there are no missing and no unlocalized entries. */
	UFUNCTION(BlueprintPure, Category = "Localization|QA")
	bool IsAllGreen() const { return MissingKeys.IsEmpty() && UnlocalizedKeys.IsEmpty(); }
};

/**
 * GameInstance-scoped localization QA utility. Dev/shipping-gated: pseudo-localization is compiled
 * out of UE_BUILD_SHIPPING, and ShouldCreateSubsystem returns false in shipping so this subsystem
 * never exists in a shipped game.
 *
 * RESPONSIBILITIES:
 *  - MISSING-TRANSLATION DETECTION: reads the public ULoc_DeveloperSettings::StringTableBindings,
 *    force-registers each table's asset via FStringTableRegistry (so rows are reachable even if the
 *    table was not yet loaded), then resolves every configured key through the EXISTING PUBLIC
 *    ULoc_LocalizationSubsystem::FindText for each requested culture. Reports missing / unlocalized
 *    keys per culture in an FLoc_QAReport.
 *  - PSEUDO-LOCALIZATION TOGGLE: wraps FInternationalization pseudo-localization. Gated behind
 *    !UE_BUILD_SHIPPING so it cannot ship. The toggle state is persisted on ULoc_DeveloperSettings
 *    (Config=Game, not ULoc_GameUserSettings — QA state is a dev config, not a player preference).
 *    Toggling triggers an OnCultureChanged broadcast so every system re-reads text in pseudo form.
 *
 * SAFETY: resolves all text through the PUBLIC FindText API only; never reaches into the private
 * FindBindingForKey / EnsureTableRegistered internals of the localization subsystem. Uses the engine
 * FStringTableRegistry directly (engine public API) to pre-register table assets before scanning.
 *
 * GC: GameInstance subsystem; holds no UObject cross-refs beyond transient local scope. No tick,
 * timer, or bus listener — pure on-demand API.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_LocalizationQASubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Returns false in shipping so this subsystem is never instantiated in a packaged build. In editor
	 * and development it is always created (no additional condition needed).
	 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem

	/**
	 * Scan every configured string-table key (from ULoc_DeveloperSettings::StringTableBindings) against
	 * the supplied cultures and return the full report. An empty Cultures array scans only the currently
	 * active culture. Each culture's table assets are force-registered via FStringTableRegistry before the
	 * scan so rows are reachable regardless of load state.
	 *
	 * @param Cultures Culture codes to scan (e.g. {"en", "fr-FR", "ja"}). Empty -> active culture only.
	 * @return The aggregated QA report (MissingKeys + UnlocalizedKeys; empty means all-green).
	 */
	UFUNCTION(BlueprintCallable, Category = "Localization|QA")
	FLoc_QAReport ScanMissingTranslations(const TArray<FString>& Cultures);

	/**
	 * Enable or disable pseudo-localization for QA layout / translation-coverage testing.
	 * Gated at runtime by !UE_BUILD_SHIPPING (compiled through in non-shipping, no-op in shipping).
	 * When enabled, FInternationalization pseudo-localizes all resolved FText values (accent expansion /
	 * bracket wrapping / length padding), and the active-culture-changed delegate fires so all text
	 * consumers re-read.
	 *
	 * @param bEnabled  True to enable pseudo-localization; false to restore real text.
	 */
	UFUNCTION(BlueprintCallable, Category = "Localization|QA")
	void SetPseudoLocalizationEnabled(bool bEnabled);

	/**
	 * Whether pseudo-localization is currently enabled (always false in shipping).
	 */
	UFUNCTION(BlueprintPure, Category = "Localization|QA")
	bool IsPseudoLocalizationEnabled() const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/**
	 * Force-load and register a string-table asset via FStringTableRegistry (public engine API) so its
	 * rows are accessible to FindText before the table is normally loaded by the asset manager. No-op if
	 * the table is already registered or if the soft path is unset.
	 */
	void ForceRegisterTable(const FSoftObjectPath& TableAsset) const;

	/**
	 * Perform the scan for a single culture: temporarily request that culture from the engine, scan all
	 * configured keys, then restore the original culture. Appends failures to Report.
	 */
	void ScanForCulture(const FString& Culture, FLoc_QAReport& Report);

	/** Cached pseudo-loc state so IsPseudoLocalizationEnabled can answer without re-querying the engine. */
	bool bPseudoLocalizationEnabled = false;
};
