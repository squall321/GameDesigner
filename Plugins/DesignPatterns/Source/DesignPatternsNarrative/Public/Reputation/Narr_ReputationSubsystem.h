// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Reputation/Seam_Reputation.h"
#include "Persist/Seam_Persistable.h"
#include "UObject/WeakInterfacePtr.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Narr_ReputationSubsystem.generated.h"

class IWorldHub_Queryable;
class UWorldHub_StateHubSubsystem;

/**
 * One discrete standing tier with the minimum numeric reputation at which it applies.
 *
 * Authored on UNarr_ReputationSubsystem (EditAnywhere) so designers map raw reputation onto named tiers
 * (Hostile / Neutral / Friendly / Honored ...). Tiers are evaluated from highest MinReputation downward;
 * the first whose MinReputation a value meets is the active tier.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_ReputationTier
{
	GENERATED_BODY()

	/** The tag returned by ISeam_Reputation::GetReputationTier when this tier is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Reputation", meta = (Categories = "Reputation.Tier"))
	FGameplayTag TierTag;

	/** The minimum (inclusive) numeric reputation at which this tier becomes active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Reputation")
	float MinReputation = 0.f;
};

/**
 * The durable save record for the reputation subsystem.
 *
 * Mirrors the story director's record pattern. Holds the LOCAL tracking maps the subsystem needs to resume
 * (faction standings + per-NPC affinity). The canonical values also live as world-hub counters (which
 * replicate + save through the hub); this record re-seeds the in-memory maps after a load so a standalone
 * / host resumes without re-deriving from the hub. Only flat tag->float pairs are stored (no refs).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_ReputationSaveRecord
{
	GENERATED_BODY()

	/** Faction standing keys. */
	UPROPERTY(SaveGame)
	TArray<FGameplayTag> FactionTags;

	/** Faction standing values (parallel to FactionTags). */
	UPROPERTY(SaveGame)
	TArray<float> FactionValues;

	/** Per-NPC affinity keys. */
	UPROPERTY(SaveGame)
	TArray<FGameplayTag> NpcTags;

	/** Per-NPC affinity values (parallel to NpcTags). */
	UPROPERTY(SaveGame)
	TArray<float> NpcValues;
};

/** Fired locally when a faction standing or NPC affinity changes (for UI / quest binding). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNarr_OnReputationChanged, FGameplayTag, Tag, float, NewValue);

/**
 * THE authoritative reputation owner for the whole plugin.
 *
 * GameInstance-scoped so standing survives level travel. Implements the shared ISeam_Reputation seam
 * (AActor*-keyed reads), so dialogue gates, RPG quest-stage gates, and the economy (merchant discounts /
 * bank access) read standing through one seam without depending on the Narrative module. Stores standings
 * as world-hub counters at FWorldHub_Scope::Faction(tag) (faction reputation) and FWorldHub_Scope::Entity(id)
 * (per-NPC affinity) via the concrete UWorldHub_StateHubSubsystem (private World dep, resolved in .cpp,
 * passing Scope explicitly) so the canonical values replicate + save through the hub's single path.
 *
 * Authority: AddFactionReputation / AddNpcAffinity guard HasWorldAuthority() at the TOP and no-op on
 * clients (clients see standing via hub replication + bus). Reads FAIL-CLOSED: HasReputation reports
 * whether a real provider/value exists so an absent provider is distinguishable from neutral.
 *
 * Self-registers under DP.Service.Narrative.Reputation. Persists like the story director, registering as a
 * narrative ISeam_Persistable participant gathered by UNarr_SaveGame.
 */
UCLASS()
class DESIGNPATTERNSNARRATIVE_API UNarr_ReputationSubsystem
	: public UDP_GameInstanceSubsystem
	, public ISeam_Reputation
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * GameInstance subsystems have no HasWorldAuthority(); derive it from the current world's net mode.
	 * True on server / standalone / listen host. Every standing mutator gates on this.
	 */
	bool HasWorldAuthority() const;

	// ---- Authoritative mutators (AUTHORITY ONLY) ----------------------------------------------

	/**
	 * Add Delta to FactionTag's standing and @return the new value. AUTHORITY ONLY (no-op-returns-current on
	 * clients). Writes the canonical value as a hub counter at Faction scope and broadcasts a change.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Reputation")
	float AddFactionReputation(FGameplayTag FactionTag, float Delta);

	/**
	 * Add Delta to the per-NPC affinity addressed by NpcTag and @return the new value. AUTHORITY ONLY.
	 * Stored as a hub counter at the NPC's entity scope (derived from NpcTag via a stable name-based guid).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Reputation")
	float AddNpcAffinity(FGameplayTag NpcTag, float Delta);

	/** Directly set FactionTag's standing. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Reputation")
	void SetFactionReputation(FGameplayTag FactionTag, float Value);

	/** Read FactionTag's standing (0 when unknown). Safe on clients. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Reputation")
	float GetFactionReputation(FGameplayTag FactionTag) const;

	/** Read the per-NPC affinity for NpcTag (0 when unknown). Safe on clients. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Reputation")
	float GetNpcAffinity(FGameplayTag NpcTag) const;

	/** The tier configuration mapping raw standing onto named tiers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Reputation")
	TArray<FNarr_ReputationTier> Tiers;

	/** Fired locally (server + clients) whenever a faction/NPC standing changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Narrative|Reputation")
	FNarr_OnReputationChanged OnReputationChanged;

	// ---- ISeam_Reputation (raw C++ virtuals, fail-closed) -------------------------------------
	virtual bool HasReputation(const AActor* Subject) const override;
	virtual float GetReputation(const AActor* Subject, FGameplayTag FactionTag) const override;
	virtual FGameplayTag GetReputationTier(const AActor* Subject, FGameplayTag FactionTag) const override;
	virtual bool MeetsStanding(const AActor* Subject, FGameplayTag FactionTag, int32 MinStanding) const override;

	// ---- ISeam_Persistable --------------------------------------------------------------------
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Map a raw reputation value onto its active tier tag using Tiers (invalid if none configured). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Reputation")
	FGameplayTag ResolveTier(float Value) const;

private:
	/** In-memory faction standings (mirror of the hub counters; source for save). */
	TMap<FGameplayTag, float> FactionStandings;

	/** In-memory per-NPC affinity (mirror of the hub counters; source for save). */
	TMap<FGameplayTag, float> NpcAffinities;

	/** Cached read seam onto the world hub. Non-owning. */
	mutable TWeakInterfacePtr<IWorldHub_Queryable> CachedHubQuery;

	/** Resolve the concrete world hub for authoritative writes / typed reads (null on failure). */
	UWorldHub_StateHubSubsystem* GetHubAuthority() const;

	/** Self-register under DP.Service.Narrative.Reputation (WeakObserved). */
	void RegisterServices();

	/** Derive a stable entity scope for an NPC tag (so per-NPC affinity has a hub scope without an actor). */
	struct FWorldHub_Scope MakeNpcScope(const FGameplayTag& NpcTag) const;

	/** Write a faction/NPC value to the hub (authority) and refresh the in-memory mirror + broadcast. */
	float ApplyStanding(bool bFaction, const FGameplayTag& Tag, float NewValue);
};
