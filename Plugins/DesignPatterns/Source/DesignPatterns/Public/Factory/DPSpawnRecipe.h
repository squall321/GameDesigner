// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DPSpawnRecipe.generated.h"

class AActor;

/**
 * Per-call parameters threaded through a spawn. Designers configure the *static* shape of a
 * spawn on a UDP_SpawnRecipe; FDP_SpawnParams carries the *dynamic*, call-site overrides
 * (where, who, and what behaviour collision policy to use). Kept a plain USTRUCT (no UObject
 * refs that need GC ownership) so it can be copied freely and passed by const-ref on hot paths.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_SpawnParams
{
	GENERATED_BODY()

	/** Identity of the thing to spawn. A factory subsystem maps this tag to a recipe/factory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Factory")
	FGameplayTag IdentityTag;

	/** World transform for the spawned actor. Combined with the recipe's default transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Factory")
	FTransform Transform = FTransform::Identity;

	/**
	 * Optional owner for the spawned actor (drives net relevancy / ownership). Non-owning here:
	 * the spawned actor takes a strong ref via SetOwner; this struct only forwards the pointer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Factory")
	TWeakObjectPtr<AActor> Owner;

	/** Optional instigator pawn for damage/credit attribution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Factory")
	TWeakObjectPtr<APawn> Instigator;

	/** Collision handling override applied at SpawnActor time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Factory")
	ESpawnActorCollisionHandlingMethod CollisionHandling = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	/**
	 * When true the factory subsystem may route this spawn through the object pool (if present)
	 * instead of constructing a fresh actor. Pure-pool semantics are opt-in per call.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Factory")
	bool bAllowPooling = true;

	/** Free-form tags forwarded to the created actor's factory hook (e.g. faction, tier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Factory")
	FGameplayTagContainer ContextTags;

	FDP_SpawnParams() = default;
};

/**
 * A designer-authored description of *what* to spawn for a given identity. This is data, not
 * behaviour: it names the actor class, the identity tag, and a default transform/owner policy.
 * The matching UDP_SpawnFactory turns a recipe + FDP_SpawnParams into a live actor.
 *
 * Being a UPrimaryDataAsset it participates in the asset-manager primary-asset graph, so a
 * project can cook/scan recipes by type and the factory subsystem can discover them by tag.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNS_API UDP_SpawnRecipe : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UDP_SpawnRecipe();

	/** Identity tag this recipe answers for. Used as the primary-asset name and registry key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "DesignPatterns|Factory")
	FGameplayTag IdentityTag;

	/** Actor class to instantiate. Soft so recipes can be scanned without loading gameplay code. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Factory", meta = (AllowAbstract = "false"))
	TSoftClassPtr<AActor> ActorClass;

	/** Default transform applied before any per-call FDP_SpawnParams override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Factory")
	FTransform DefaultTransform = FTransform::Identity;

	/** Default collision handling when a call does not override it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Factory")
	ESpawnActorCollisionHandlingMethod DefaultCollisionHandling = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	/** Tags describing this recipe (category/tier/faction) for filtered discovery. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Factory")
	FGameplayTagContainer RecipeTags;

	/**
	 * Synchronously resolve the actor class, loading the soft pointer if needed. Returns nullptr
	 * if unset or the asset fails to load. Prefer async loading on hot paths in shipping code.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Factory")
	TSubclassOf<AActor> ResolveActorClass() const;

	/** Build a default FDP_SpawnParams seeded from this recipe (identity + transform + collision). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Factory")
	FDP_SpawnParams MakeDefaultParams() const;

	//~ Begin UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

	/** Primary-asset type used by the asset manager to group spawn recipes. */
	static const FPrimaryAssetType PrimaryAssetType;
};
