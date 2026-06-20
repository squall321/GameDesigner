// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Mod_ContentPackDescriptor.generated.h"

/**
 * A semantic version (Major.Minor.Patch) for packs, engine and game. Plain integers so it serializes
 * trivially and compares without parsing. Used for pack version, dependency version floors, and the
 * min-engine / min-game compatibility gates.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_SemVer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Mod", meta = (ClampMin = "0"))
	int32 Major = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Mod", meta = (ClampMin = "0"))
	int32 Minor = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Mod", meta = (ClampMin = "0"))
	int32 Patch = 0;

	FMod_SemVer() = default;
	FMod_SemVer(int32 InMajor, int32 InMinor, int32 InPatch) : Major(InMajor), Minor(InMinor), Patch(InPatch) {}

	/** True when this version is at least Other (i.e. satisfies a minimum-version requirement of Other). */
	bool IsAtLeast(const FMod_SemVer& Other) const
	{
		if (Major != Other.Major) { return Major > Other.Major; }
		if (Minor != Other.Minor) { return Minor > Other.Minor; }
		return Patch >= Other.Patch;
	}

	/** True if all components are zero (treated as "unset" — an unset minimum gate is satisfied by anything). */
	bool IsZero() const { return Major == 0 && Minor == 0 && Patch == 0; }

	FString ToString() const { return FString::Printf(TEXT("%d.%d.%d"), Major, Minor, Patch); }

	bool operator==(const FMod_SemVer& Other) const { return Major == Other.Major && Minor == Other.Minor && Patch == Other.Patch; }
};

/**
 * A declared dependency on another pack: the depended-on pack id plus the minimum version that
 * satisfies it. The manager uses these to build the dependency graph (topological mount order) and
 * to reject a pack whose hard dependencies are missing or too old. Optional dependencies only warn.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_PackDependency
{
	GENERATED_BODY()

	/** The required pack's identity (child of DP.Mod.Pack). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Mod", meta = (Categories = "DP.Mod.Pack"))
	FGameplayTag DependencyId;

	/** Minimum acceptable version of the dependency. Zero means "any version present". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FMod_SemVer MinVersion;

	/**
	 * When true a missing/too-old dependency only WARNS (pack still mounts after its present deps);
	 * when false it is a hard FAIL that rejects the mount. Optional deps never affect mount ORDER if
	 * the dependency is absent.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Mod")
	bool bOptional = false;
};

/**
 * Authoring descriptor for one content pack — a tag-identified UDP_DataAsset that travels inside the
 * pack and describes everything the content manager and validator need WITHOUT mounting or executing
 * anything: the pack's stable id, its version, what it depends on, which engine content roots it
 * contributes, and the min engine / game versions it requires.
 *
 * It is pure metadata. It references no code, spawns nothing, and carries no executable payload — the
 * "never auto-execute mod code" rule is upheld structurally because a descriptor can only describe
 * content roots and dependencies, never a module to load.
 *
 * The pack's identity is the inherited DataTag (a child of DP.Mod.Pack), so the descriptor is also a
 * normal data-registry asset addressable by tag.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Mod Content Pack Descriptor"))
class DESIGNPATTERNSMODCONTENT_API UMod_ContentPackDescriptor : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UMod_ContentPackDescriptor();

	/**
	 * The pack's identity. Convenience alias around the inherited DataTag so call sites read
	 * "PackId" without depending on the base field name. Always equals DataTag.
	 */
	FGameplayTag GetPackId() const { return DataTag; }

	/** This pack's own version. The dependency / version gates of OTHER packs compare against this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pack")
	FMod_SemVer PackVersion;

	/**
	 * Packs this one requires (and the minimum versions). The manager mounts dependencies first
	 * (topological order) and rejects this pack if a hard dependency is missing, too old, or part of
	 * a cycle. Optional dependencies (bOptional) only warn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pack")
	TArray<FMod_PackDependency> Dependencies;

	/**
	 * Engine content roots this pack contributes, as mount-point-relative virtual paths (e.g.
	 * "/MyPack/Maps"). Used by the validator to check the pack stays inside its sandbox and by
	 * tooling to enumerate the pack's content. Informational for plugin packs (the engine derives
	 * the root); authoritative for raw pak packs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pack", meta = (LongPackageName))
	TArray<FString> ContentRoots;

	/**
	 * Minimum host ENGINE version the pack supports. The manager refuses to mount a pack that
	 * requires a newer engine than the running build. Zero means "no engine floor".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Compatibility")
	FMod_SemVer MinEngineVersion;

	/**
	 * Minimum host GAME (project) version the pack supports. Compared against the project version the
	 * manager reads from settings. Zero means "no game floor".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Compatibility")
	FMod_SemVer MinGameVersion;

	/**
	 * Optional human-facing author / attribution string for store / UGC display. Cosmetic; never used
	 * for identity, ordering, or any security decision.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	FString Author;

	//~ Begin UDP_DataAsset
	/** Group all pack descriptors under a single asset-manager bucket so they enumerate together. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/**
	 * In addition to the base empty-DataTag check, flags: a PackId not under DP.Mod.Pack, a
	 * self-dependency, and duplicate dependency ids.
	 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
