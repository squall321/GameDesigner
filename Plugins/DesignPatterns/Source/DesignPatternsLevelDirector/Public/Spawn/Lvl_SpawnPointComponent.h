// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "Lvl_SpawnPointComponent.generated.h"

/**
 * A single team/tag-filtered spawn point that contributes its world transform to an enclosing
 * spawn region (ALvl_SpawnRegionVolume) or directly to any provider that queries components.
 *
 * Attach one or more of these under a spawn-region volume (or any actor) to author exact spawn
 * transforms by hand, instead of (or in addition to) the volume's procedurally-sampled points.
 * The component carries filter tags (team / faction / role) so a caller asking for a specific
 * filter only receives matching points. Purely local data: nothing here replicates or persists.
 */
UCLASS(ClassGroup = "DesignPatterns|LevelDirector", meta = (BlueprintSpawnableComponent),
	HideCategories = ("Sockets", "Tags", "ComponentTick", "ComponentReplication", "Activation", "Cooking", "AssetUserData", "Collision"))
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_SpawnPointComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	ULvl_SpawnPointComponent();

	/**
	 * Tags describing this spawn point (e.g. Team.Red, Role.Sniper, Faction.Raiders). A provider
	 * query with filter F includes this point when F is invalid (match-all) or this container has F
	 * or a child of F. Authored as data; no hardcoded gameplay semantics live in code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn")
	FGameplayTagContainer FilterTags;

	/**
	 * When false the point is ignored by all queries (a designer can disable a point without
	 * deleting it). Local toggle only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn")
	bool bEnabled = true;

	/**
	 * Relative weight used by callers that pick a subset of points (higher = more likely). Carried
	 * through so a weighted picker can honour designer intent; the component itself does no picking.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn", meta = (ClampMin = "0.0"))
	float SelectionWeight = 1.f;

	/** True if this point is enabled AND its filter tags match the given filter (empty = match all). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Spawn")
	bool MatchesFilter(FGameplayTag Filter) const;

	/** The point's world transform (component transform). Used directly as a spawn transform. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Spawn")
	FTransform GetSpawnTransform() const { return GetComponentTransform(); }

#if WITH_EDITORONLY_DATA
	/** Editor billboard sprite so designers can see and pick spawn points in the viewport. */
	UPROPERTY(Transient)
	TObjectPtr<class UBillboardComponent> EditorSprite;
#endif

protected:
	//~ Begin UActorComponent
	virtual void OnRegister() override;
	//~ End UActorComponent
};
