// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Persist/Seam_Persistable.h"
#include "USurv_KnowledgeComponent.generated.h"

class USurv_TechNode;
class USurv_ResourceStoreComponent;

/**
 * Save record for the per-player knowledge ledger. Carried as an FInstancedStruct through
 * ISeam_Persistable so the save object never knows the concrete type. SaveGame fields only.
 */
USTRUCT()
struct FSurv_KnowledgeSaveRecord
{
	GENERATED_BODY()

	/** Tech tags the player has researched. */
	UPROPERTY(SaveGame)
	FGameplayTagContainer KnownTechTags;

	/** Recipe tags the player has discovered. */
	UPROPERTY(SaveGame)
	FGameplayTagContainer DiscoveredRecipeTags;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnTechGranted, FGameplayTag, TechTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnRecipeDiscovered, FGameplayTag, RecipeTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSurv_OnResearchProgress, FGameplayTag, TechTag, float, Progress01);

/**
 * Per-player replicated tech / discovered-recipe ledger and the single source of truth for crafting
 * and building gates.
 *
 * - The ledgers replicate (ReplicatedUsing) so clients can drive UI; all mutators are AUTHORITY-ONLY.
 * - Clients route intent through ServerRequestResearch (validated server-side: prerequisites + cost).
 * - Research can take time: the head request runs on a world timer; only one research runs at a time.
 * - Persists via ISeam_Persistable (authority-guarded RestoreState).
 *
 * The advanced crafting component and building placement component hold a pointer to this and consult
 * HasTech / IsRecipeUnlocked before allowing a craft/build (advisory on the base path; enforced on the
 * server-validated intent path).
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API USurv_KnowledgeComponent
	: public UActorComponent
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	USurv_KnowledgeComponent();

	//~ Begin UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Queries (client-safe; read replicated state) ----

	/** True if TechTag is in the known-tech ledger (hierarchy-aware: a child satisfies a parent query). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Knowledge")
	bool HasTech(FGameplayTag TechTag) const;

	/** True if ALL of the tags in Required are known. Empty container = always true. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Knowledge")
	bool HasAllTech(const FGameplayTagContainer& Required) const;

	/** True if RecipeTag has been discovered (or was never discovery-gated — caller decides). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Knowledge")
	bool IsRecipeUnlocked(FGameplayTag RecipeTag) const;

	/** Snapshot of all known tech tags. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Knowledge")
	const FGameplayTagContainer& GetKnownTech() const { return KnownTechTags; }

	/** Snapshot of all discovered recipe tags. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Knowledge")
	const FGameplayTagContainer& GetDiscoveredRecipes() const { return DiscoveredRecipeTags; }

	/** Normalized 0..1 progress of the active research (0 when idle). Client-safe (derived). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Knowledge")
	float GetActiveResearchProgress() const;

	/** Tech tag currently being researched (invalid when idle). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Knowledge")
	FGameplayTag GetActiveResearchTech() const { return ActiveResearchTechTag; }

	// ---- Authority mutators ----

	/** Grant a tech tag directly (e.g. from a quest reward). AUTHORITY-ONLY. Returns true if newly added. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Knowledge")
	bool GrantTech(FGameplayTag TechTag);

	/** Discover a recipe directly. AUTHORITY-ONLY. Returns true if newly discovered. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Knowledge")
	bool DiscoverRecipe(FGameplayTag RecipeTag);

	/**
	 * Begin researching the tech node identified by TechNodeTag, paying its cost from ResourceStore
	 * and starting its research timer. AUTHORITY-ONLY. Validates prerequisites, idle state, and cost.
	 * Returns true if research was started (or instantly completed for a 0-time node).
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Knowledge")
	bool BeginResearch(FGameplayTag TechNodeTag);

	/** Abort the active research WITHOUT refunding (the cost was consumed up front). AUTHORITY-ONLY. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Knowledge")
	void CancelResearch();

	// ---- Client intent ----

	/** Client -> server: request research of TechNodeTag. Server re-validates everything. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestResearch(FGameplayTag TechNodeTag);

	// ---- ISeam_Persistable ----
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;

	/** Optional resource store research costs are paid from. If null, costs are treated as free. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Knowledge")
	TObjectPtr<USurv_ResourceStoreComponent> ResourceStore;

	/** Fired (server + clients via OnRep) when a tech tag is added. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Knowledge")
	FSurv_OnTechGranted OnTechGranted;

	/** Fired when a recipe is discovered. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Knowledge")
	FSurv_OnRecipeDiscovered OnRecipeDiscovered;

	/** Fired periodically while research progresses (server local; UI may poll GetActiveResearchProgress). */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Knowledge")
	FSurv_OnResearchProgress OnResearchProgress;

protected:
	/** Replicated known-tech ledger. */
	UPROPERTY(ReplicatedUsing = OnRep_KnownTech)
	FGameplayTagContainer KnownTechTags;

	/** Replicated discovered-recipe ledger. */
	UPROPERTY(ReplicatedUsing = OnRep_Discovered)
	FGameplayTagContainer DiscoveredRecipeTags;

	/** Tech tag currently being researched (replicated so clients can show a research bar). */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveResearch)
	FGameplayTag ActiveResearchTechTag;

	/** Server world-time when active research completes (replicated for client-derived progress). */
	UPROPERTY(Replicated)
	float ResearchCompleteWorldTime = 0.f;

	/** Server world-time when active research started (replicated for client-derived progress). */
	UPROPERTY(Replicated)
	float ResearchStartWorldTime = 0.f;

	/** Mirror of KnownTechTags at last broadcast, to diff new entries on OnRep. */
	FGameplayTagContainer LastBroadcastTech;

	/** Mirror of DiscoveredRecipeTags at last broadcast, to diff new entries on OnRep. */
	FGameplayTagContainer LastBroadcastRecipes;

	/** Timer driving completion of the active research. */
	FTimerHandle ResearchTimerHandle;

	UFUNCTION()
	void OnRep_KnownTech();

	UFUNCTION()
	void OnRep_Discovered();

	UFUNCTION()
	void OnRep_ActiveResearch();

	/** True on the network authority. Gate every mutator on this. */
	bool HasAuthorityToMutate() const;

	/** Current world time seconds (0 if no world). */
	float GetWorldTimeSeconds() const;

	/** Resolve a tech node asset by tag through the core registry (nullable). */
	USurv_TechNode* ResolveTechNode(const FGameplayTag& TechNodeTag) const;

	/** Server timer callback: completes the active research, granting its tech tag. */
	void HandleResearchComplete();

	/** Broadcast OnTechGranted for tech tags present now but not in LastBroadcastTech, then resync mirror. */
	void BroadcastTechDeltas();

	/** Broadcast OnRecipeDiscovered for newly-present recipe tags, then resync mirror. */
	void BroadcastRecipeDeltas();

	/** Broadcast a bus notification that the knowledge ledger changed (local; derived from replicated state). */
	void NotifyKnowledgeChanged() const;
};
