// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_ModResolution.generated.h"

/**
 * A conflict where two or more mounted/eligible packs contribute an OVERRIDE for the SAME data tag.
 *
 * The ModContent registry resolves these by its documented precedence policy (highest LoadPriority,
 * ties to the later-mounted pack). When a project wants to override THAT decision (e.g. always prefer
 * a curated pack, or refuse a conflict outright) it implements ISeam_ModResolutionPolicy and inspects
 * this struct. Pure data; never replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FMod_TagConflict
{
	GENERATED_BODY()

	/** The contended data identity (matches UDP_DataAsset::DataTag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod")
	FGameplayTag DataTag;

	/** The pack ids (children of DP.Mod.Pack) that all claim to provide/override DataTag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod", meta = (Categories = "DP.Mod.Pack"))
	TArray<FGameplayTag> Contenders;

	/**
	 * The winner chosen by the DEFAULT precedence policy, supplied so a policy that only wants to tweak
	 * specific conflicts can return this for everything else. Invalid if the default could not pick one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Mod", meta = (Categories = "DP.Mod.Pack"))
	FGameplayTag DefaultWinner;
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ModResolutionPolicy : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam: an OPTIONAL hook to override how ModContent resolves load order and data-tag conflicts.
 *
 * The manager already ships a deterministic policy: a topological sort over declared dependencies, with
 * a configurable tie-breaker among independent packs, and (in the registry) highest-LoadPriority /
 * latest-mount precedence for overlapping overrides. A project that needs a curated or game-specific
 * resolution implements this seam and registers it under DP.Service.Mod.Resolution; the diagnostics
 * resolver / registry resolve it WEAKLY (TWeakInterfacePtr, pruned on use).
 *
 * INERT DEFAULT: when no policy is registered the consumer keeps the manager's existing ResolveMountOrder
 * and the registry's existing precedence — the default *_Implementation here simply returns the
 * already-computed default winner / a neutral score, so registering the policy is purely additive.
 *
 * Game-thread, pure, side-effect-free: a policy only RANKS and PICKS; it never mounts or executes.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ModResolutionPolicy
{
	GENERATED_BODY()

public:
	/**
	 * Pick the winning pack id for a data-tag conflict. The default returns Conflict.DefaultWinner so an
	 * unoverridden policy never changes the manager's precedence. A returned id that is not among the
	 * contenders is ignored by the consumer (falls back to the default winner).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mod")
	FGameplayTag ResolveConflict(const FMod_TagConflict& Conflict) const;

	/**
	 * Score a pack for load-order ranking among otherwise-independent packs (higher sorts later, so a
	 * higher-scored pack wins file-path collisions). The default returns 0 (neutral), preserving the
	 * manager's existing tie-break ordering. PackId is a child of DP.Mod.Pack.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mod")
	int32 ScoreLoadOrder(FGameplayTag PackId) const;
};
