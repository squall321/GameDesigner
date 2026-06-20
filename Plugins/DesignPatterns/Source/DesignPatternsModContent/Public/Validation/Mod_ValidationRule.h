// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Seam/Mod_PackValidator.h"   // EMod_ValidationResult, FMod_ValidationMessage (shared seam types)
#include "Mod_ValidationRule.generated.h"

/**
 * One composable, designer-authorable validation check run over a single asset of a content pack.
 *
 * EditInlineNew + Abstract: rules are instanced directly inside a project policy (the validation rule
 * list) so a project assembles its own rule set in the editor without code, and never spawned alone.
 * Validate is a BlueprintNativeEvent so a project can add a pure-Blueprint rule.
 *
 * A rule returns an FMod_ValidationMessage using the SHARED seam severity (EMod_ValidationResult:
 * Pass / Warn / Fail) so its verdict aggregates directly into the FMod_ValidationReport the manager
 * and the IMod_PackValidator seam already speak — there is no second, parallel severity vocabulary.
 *
 * Rules MUST be pure / side-effect-free: they inspect the asset and return a verdict; they never mount,
 * load arbitrary code, or mutate the asset. This is part of the structural "never auto-execute mod
 * code" guarantee — validation only ever READS already-mounted-but-inactive content.
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, Blueprintable, CollapseCategories)
class DESIGNPATTERNSMODCONTENT_API UMod_ValidationRule : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Evaluate this rule against Asset and return a diagnostic.
	 *
	 * Asset is the loaded UDP_DataAsset (or other UObject) under test; it may be null if the asset
	 * failed to load, and a robust rule treats null as a Fail. A clean result has Severity == Pass and
	 * an empty Detail. The default native implementation returns Pass so a base/unconfigured rule is
	 * inert (an empty rule slot never spuriously rejects content).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ModContent|Validation")
	FMod_ValidationMessage Validate(const UObject* Asset) const;
	virtual FMod_ValidationMessage Validate_Implementation(const UObject* Asset) const;

	/** Short identifier for logs/reports (defaults to the class name). Override for friendlier output. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "ModContent|Validation")
	FName GetRuleId() const;
	virtual FName GetRuleId_Implementation() const;

protected:
	/** Build a Pass message (no detail). */
	static FMod_ValidationMessage MakePass();

	/** Build a message at the given severity with a machine-readable reason tag and localizable detail. */
	static FMod_ValidationMessage MakeMessage(EMod_ValidationResult Severity, const FGameplayTag& Reason, const FText& Detail);
};

/**
 * Rule: the asset must carry a DataTag that is a descendant of (or equal to) a required root tag.
 *
 * Use to enforce that every asset a pack contributes lives under the project's sanctioned content
 * namespace (e.g. all pack data under DP.Mod.Content) so a pack cannot masquerade as core content by
 * claiming a base DataTag. Severity on failure is configurable.
 */
UCLASS(meta = (DisplayName = "Required Tag Present"))
class DESIGNPATTERNSMODCONTENT_API UMod_ValidationRule_RequiredTagPresent : public UMod_ValidationRule
{
	GENERATED_BODY()

public:
	/**
	 * The asset's DataTag must match (exact or child of) this root. When unset, the rule only checks
	 * that the asset HAS a non-empty DataTag at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Validation")
	FGameplayTag RequiredRoot;

	/** Severity to emit when the requirement is not met. Defaults to Fail (hard-reject). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Validation")
	EMod_ValidationResult FailureSeverity = EMod_ValidationResult::Fail;

	virtual FMod_ValidationMessage Validate_Implementation(const UObject* Asset) const override;
	virtual FName GetRuleId_Implementation() const override;
};

/**
 * Rule: no SOFT object/class reference on the asset may dangle (point at content that does not exist).
 *
 * A pack whose data assets reference missing meshes/classes would fail at point-of-use deep in
 * gameplay; this catches it at mount. Walks the asset's reflected TSoftObjectPtr / TSoftClassPtr /
 * FSoftObjectPath properties and confirms each non-null target resolves in the AssetRegistry WITHOUT
 * loading it.
 */
UCLASS(meta = (DisplayName = "No Dangling Soft Reference"))
class DESIGNPATTERNSMODCONTENT_API UMod_ValidationRule_NoDanglingSoftRef : public UMod_ValidationRule
{
	GENERATED_BODY()

public:
	/** Severity to emit when a dangling soft reference is found. Defaults to Warn (optional refs exist). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Validation")
	EMod_ValidationResult FailureSeverity = EMod_ValidationResult::Warn;

	virtual FMod_ValidationMessage Validate_Implementation(const UObject* Asset) const override;
	virtual FName GetRuleId_Implementation() const override;
};

/**
 * Rule: the asset's declared content version must be compatible with the host's supported range.
 *
 * Reads an int32 version property (absent = version 0) and checks it sits within
 * [MinSupportedVersion, MaxSupportedVersion]. Lets the host reject content built for an incompatible
 * (too old / too new) data schema before it is ever used.
 */
UCLASS(meta = (DisplayName = "Version Compatible"))
class DESIGNPATTERNSMODCONTENT_API UMod_ValidationRule_VersionCompatible : public UMod_ValidationRule
{
	GENERATED_BODY()

public:
	/** Name of the int property carrying the asset's content/schema version. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Validation")
	FName VersionPropertyName = TEXT("ContentVersion");

	/** Lowest content version this host accepts (inclusive). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Validation", meta = (ClampMin = "0"))
	int32 MinSupportedVersion = 0;

	/** Highest content version this host accepts (inclusive). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Validation", meta = (ClampMin = "0"))
	int32 MaxSupportedVersion = 1;

	/** Severity to emit when the version is out of range. Defaults to Fail. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Validation")
	EMod_ValidationResult FailureSeverity = EMod_ValidationResult::Fail;

	virtual FMod_ValidationMessage Validate_Implementation(const UObject* Asset) const override;
	virtual FName GetRuleId_Implementation() const override;
};

/**
 * Rule: the asset's package path must sit under one of the sanctioned, sandboxed content roots.
 *
 * The structural sandbox guarantee: a pack may only contribute assets that live under an allowed mount
 * root (e.g. /Game/Mods/ or the pack's own plugin content root). An asset whose package is outside
 * every allowed root is a sandbox escape and is rejected. Roots are matched as path prefixes.
 */
UCLASS(meta = (DisplayName = "Within Content Root"))
class DESIGNPATTERNSMODCONTENT_API UMod_ValidationRule_WithinContentRoot : public UMod_ValidationRule
{
	GENERATED_BODY()

public:
	/**
	 * Allowed package-path prefixes (e.g. "/Game/Mods/", "/MyModPlugin/"). An asset passes if its
	 * package path begins with ANY of these. Empty = rely solely on the per-pack roots injected by the
	 * validator subsystem at call time (see UMod_ContentValidatorSubsystem::SetInjectedRoots).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Validation")
	TArray<FString> AllowedRootPrefixes;

	/** Severity to emit when the asset is outside every allowed root. Defaults to Fail. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Validation")
	EMod_ValidationResult FailureSeverity = EMod_ValidationResult::Fail;

	/**
	 * Per-pack roots injected by the validator subsystem before it runs this rule over a pack's assets
	 * (the pack's own sandboxed roots). Transient; combined with AllowedRootPrefixes at evaluation time.
	 */
	UPROPERTY(Transient)
	TArray<FString> InjectedPackRoots;

	virtual FMod_ValidationMessage Validate_Implementation(const UObject* Asset) const override;
	virtual FName GetRuleId_Implementation() const override;
};
