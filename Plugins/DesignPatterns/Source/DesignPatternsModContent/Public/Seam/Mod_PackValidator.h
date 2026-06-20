// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam/Mod_ContentSource.h"
#include "Mod_PackValidator.generated.h"

/**
 * Outcome severity of validating one pack before it is activated. The manager's mount path branches
 * on this: Pass mounts silently, Warn mounts but records/broadcasts the warnings, Fail rejects the
 * mount entirely (validate-before-activate).
 */
UENUM(BlueprintType)
enum class EMod_ValidationResult : uint8
{
	/** Pack is clean. Mount proceeds. */
	Pass,

	/**
	 * Pack mounted with non-fatal concerns (e.g. soft-missing optional dependency, cosmetic asset
	 * issue). The manager mounts it but flags it in metadata and broadcasts the warnings.
	 */
	Warn,

	/**
	 * Pack is unsafe / incompatible (bad id, sandbox escape, hard-missing dependency, version
	 * mismatch, dependency cycle). The manager REJECTS the mount; nothing is mounted or executed.
	 */
	Fail
};

/** One human-readable diagnostic produced by validation, tagged with a machine-readable reason. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_ValidationMessage
{
	GENERATED_BODY()

	/** Severity of this single message (a report's overall result is the max severity of its messages). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	EMod_ValidationResult Severity = EMod_ValidationResult::Pass;

	/** Machine-readable reason tag (child of DP.Mod, e.g. DP.Mod.Reason.MissingDependency) for tooling. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FGameplayTag Reason;

	/** Operator-facing detail. FText so it is localizable; never used for identity or persistence. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FText Detail;
};

/**
 * Aggregate validation outcome for one pack. Carried back to the manager's mount path and stored in
 * the mounted-pack metadata so tooling / UI can surface why a pack warned or was rejected.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_ValidationReport
{
	GENERATED_BODY()

	/** Overall result: the worst severity across Messages. Pass when there are no messages. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	EMod_ValidationResult Result = EMod_ValidationResult::Pass;

	/** Individual diagnostics gathered during validation. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	TArray<FMod_ValidationMessage> Messages;

	/** True when this report permits the pack to be mounted (Pass or Warn). */
	bool AllowsMount() const { return Result != EMod_ValidationResult::Fail; }

	/** Recompute Result as the worst severity present in Messages. Call after appending messages. */
	void RecomputeResult()
	{
		Result = EMod_ValidationResult::Pass;
		for (const FMod_ValidationMessage& Msg : Messages)
		{
			if (Msg.Severity > Result)
			{
				Result = Msg.Severity;
			}
		}
	}
};

UINTERFACE(BlueprintType, MinimalAPI)
class UMod_PackValidator : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam: validates a discovered pack BEFORE the manager activates (mounts) it. The concrete validator
 * is implemented by a sibling area and registered under DP.Service.Mod.Validator; the manager
 * resolves it (weakly, pruned on use) and calls it at every mount.
 *
 * The validator inspects pack metadata, dependency satisfaction, version compatibility and content
 * sandboxing. It NEVER mounts, loads, or executes the pack — it reasons over the FMod_PackInfo and
 * descriptor only. This keeps the unsafe operation (mounting) strictly behind a passing validation.
 *
 * Inert default: when no validator is registered the manager treats every pack as Pass for the
 * validator's portion, but STILL applies its own structural guards (allowlist, sandbox-root check,
 * dependency/version resolution). So removing the validator weakens content vetting but never
 * disables the manager's hard safety rails.
 */
class DESIGNPATTERNSMODCONTENT_API IMod_PackValidator
{
	GENERATED_BODY()

public:
	/**
	 * Validate one pack. KnownPacks is the full discovered set (for cross-pack dependency checks).
	 * Must be game-thread and free of mount/IO side effects beyond reading the pack's own manifest.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Mod")
	FMod_ValidationReport ValidatePack(const FMod_PackInfo& Pack, const TArray<FMod_PackInfo>& KnownPacks) const;
};
