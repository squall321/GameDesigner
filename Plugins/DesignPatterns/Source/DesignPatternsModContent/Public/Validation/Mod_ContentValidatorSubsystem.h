// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Seam/Mod_PackValidator.h"     // IMod_PackValidator, FMod_PackInfo, FMod_ValidationReport
#include "Validation/Mod_ValidationRule.h"
#include "Mod_ContentValidatorSubsystem.generated.h"

/**
 * GameInstance subsystem that VALIDATES a content pack's assets at mount (validate-before-activate).
 *
 * It implements the shared IMod_PackValidator seam and registers itself under DP.Service.Mod.Validator
 * so the content manager resolves it (weakly, pruned on use) and calls it at every mount. The same
 * entry point is exposed publicly for the editor validation commandlet, so CI and runtime apply exactly
 * the same policy.
 *
 * For each asset listed by a pack it:
 *   1. runs UDP_DataAsset::IsDataValid (editor-only engine validation) where available, and
 *   2. runs the configured UMod_ValidationRule set (ship rules: RequiredTagPresent, NoDanglingSoftRef,
 *      VersionCompatible, WithinContentRoot) — the pack's own sandboxed roots are injected into the
 *      WithinContentRoot rule so a pack is confined to its own content roots.
 * It aggregates every diagnostic into an FMod_ValidationReport whose Result is the worst severity, so
 * the manager can FAIL bad packs SAFE (a Fail blocks the mount; a Warn is surfaced but allowed).
 *
 * The validator NEVER executes pack code: it only loads-and-inspects data assets (the rules are pure).
 *
 * Rule set source: the active rules come from the configured ConfiguredRules (authored in project
 * config / injected). When that list is empty the subsystem falls back to a DEFAULT, conservative rule
 * set constructed in code (documented in BuildActiveRules) so validation is never silently skipped.
 */
UCLASS()
class DESIGNPATTERNSMODCONTENT_API UMod_ContentValidatorSubsystem
	: public UDP_GameInstanceSubsystem
	, public IMod_PackValidator
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin IMod_PackValidator
	/**
	 * Seam entry the manager calls at every mount. KnownPacks is the full discovered set (kept for
	 * parity with the seam; per-asset rules do not currently cross-reference it, but a project rule can).
	 * Delegates to ValidatePackAssets and returns the aggregated report.
	 */
	virtual FMod_ValidationReport ValidatePack_Implementation(const FMod_PackInfo& Pack, const TArray<FMod_PackInfo>& KnownPacks) const override;
	//~ End IMod_PackValidator

	/**
	 * Validate every asset in Pack and return an aggregated report. THE shared entry point for both the
	 * runtime manager (via the seam) and the editor commandlet (directly).
	 *
	 * Loads each contained asset synchronously, runs IsDataValid (editor builds) + the active rule set,
	 * injects the pack's content roots into any WithinContentRoot rule, and aggregates a worst-severity
	 * report. Pure with respect to the pack: it loads but never mounts / activates / mutates content.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Validation")
	FMod_ValidationReport ValidatePackAssets(const FMod_PackInfo& Pack) const;

	/**
	 * Convenience: true if Pack passes (Result != Fail). Warnings still allow the mount. Use when the
	 * caller only needs the go/no-go and not the detailed report.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Validation")
	bool IsPackMountable(const FMod_PackInfo& Pack) const { return ValidatePackAssets(Pack).AllowsMount(); }

	/**
	 * Replace the configured rule set at runtime (e.g. the commandlet injecting a CI-specific policy).
	 * Pass an empty array to revert to the conservative built-in default on the next BuildActiveRules.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Validation")
	void SetConfiguredRules(const TArray<UMod_ValidationRule*>& InRules);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/**
	 * Project-authored rule set (injected via SetConfiguredRules / a future settings property). When
	 * empty, BuildActiveRules synthesises the conservative default. Held as UPROPERTY so the instanced
	 * rule objects are GC-rooted.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMod_ValidationRule>> ConfiguredRules;

	/**
	 * The rule set actually run, resolved from ConfiguredRules or the built-in default. Rebuilt lazily
	 * and on SetConfiguredRules. GC-rooted.
	 */
	UPROPERTY(Transient)
	mutable TArray<TObjectPtr<UMod_ValidationRule>> ActiveRules;

	/** True once ActiveRules has been resolved this session. */
	mutable bool bRulesBuilt = false;

	/**
	 * Resolve ActiveRules from ConfiguredRules, or synthesise the documented conservative default when
	 * ConfiguredRules is empty. The default set is: RequiredTagPresent (Fail), NoDanglingSoftRef (Warn),
	 * VersionCompatible (Fail), WithinContentRoot (Fail) — structurally unsafe content is rejected while
	 * a dangling soft ref is only warned (it may be an intentionally optional reference).
	 */
	void BuildActiveRules() const;

	/**
	 * Validate a single loaded asset against IsDataValid + the active rules, injecting PackRoots into any
	 * WithinContentRoot rule. Appends every non-Pass diagnostic to InOutReport.Messages.
	 */
	void ValidateAsset(UObject* Asset, const FSoftObjectPath& AssetPath, const TArray<FString>& PackRoots, FMod_ValidationReport& InOutReport) const;

	/** Collect the sandboxed roots for a pack (descriptor ContentRoots + the pack's mount point root). */
	static TArray<FString> GatherPackRoots(const FMod_PackInfo& Pack);
};
