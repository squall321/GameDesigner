// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Social/SimAg_AffinityArray.h"
#include "SimAg_SocialComponent.generated.h"

class ISeam_EntityRelationshipRead;

/**
 * Message-bus payload broadcast when two agents have a social interaction. FInstancedStruct on
 * SimAgNativeTags::Bus_SocialInteraction.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_SocialEvent
{
	GENERATED_BODY()

	/** The agent initiating the interaction. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Social")
	FSeam_EntityId Initiator;

	/** The other agent. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Social")
	FSeam_EntityId Other;

	/** Signed affinity change applied by this interaction. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Social")
	float Delta = 0.f;
};

/** Fired (server and clients) after this agent's affinities change. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimAg_OnAffinityChanged, USimAg_SocialComponent*, SocialComponent);

/**
 * Replicated per-agent social/relationship store. Keys a signed affinity in [-1,1] by the other entity's
 * stable id (FSimAg_AffinityArray fast-array). Optionally SEEDS initial affinities from the Entity
 * module's relationship index (e.g. family/group links bias affinity) read through the raw-virtual
 * ISeam_EntityRelationshipRead seam.
 *
 * RELATIONSHIP SEAM RESOLUTION: ISeam_EntityRelationshipRead is a RAW C++ virtual seam (no UInterface
 * UClass, not TScriptInterface). It is resolved by Cast<ISeam_EntityRelationshipRead>(WorldSubsystemUObject)
 * as a TRANSIENT, NON-UPROPERTY raw pointer, re-resolved per use and guarded by HasRelationshipIndex() —
 * never stored as a UPROPERTY.
 *
 * AUTHORITY & REPLICATION: AdjustAffinity / SetAffinity guard authority at the TOP; the server owns the
 * canonical edges and flushes the fast array; clients observe. Emits Bus_SocialInteraction on changes.
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_SocialComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimAg_SocialComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/** Current affinity toward Other in [-1,1], or 0 (neutral) if no edge exists. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Social")
	float GetAffinity(const FSeam_EntityId& Other) const;

	/**
	 * Add a signed delta to the affinity toward Other (creating the edge if needed), clamped to [-1,1].
	 * AUTHORITY ONLY: early-returns on clients. Emits a social interaction event. @return the new value.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Social")
	float AdjustAffinity(const FSeam_EntityId& Other, float Delta);

	/** Set the affinity toward Other to an absolute value, clamped to [-1,1]. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Social")
	void SetAffinity(const FSeam_EntityId& Other, float NewValue);

	/**
	 * Most-liked entity among the candidates whose affinity is at/above MinAffinity. Client-safe read.
	 * @return true and fills OutOther/OutValue when at least one candidate qualifies.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Social")
	bool FindBestLiked(const TArray<FSeam_EntityId>& Candidates, float MinAffinity, FSeam_EntityId& OutOther, float& OutValue) const;

	/** Read-only snapshot of all edges (safe on clients). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Social")
	TArray<FSimAg_Affinity> GetEdges() const { return Affinities.Edges; }

	/** Fired after affinities change (server / client replication). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Social")
	FSimAg_OnAffinityChanged OnAffinityChanged;

	/** Called by the fast-array callbacks on clients to surface a replicated affinity change. */
	void HandleReplicatedChange();

protected:
	/**
	 * Link kind under the Entity module's Ent.Link.* hierarchy whose targets get a seeded affinity bias
	 * on BeginPlay (e.g. Ent.Link.Grouped => squad-mates start friendly). Empty = no seeding. Authored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Social")
	FGameplayTag SeedLinkKind;

	/** Affinity granted to each SeedLinkKind target on BeginPlay (authority only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Social", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SeedAffinity = 0.4f;

private:
	/** Replicated affinity edges (delta-serialized). */
	UPROPERTY(Replicated)
	FSimAg_AffinityArray Affinities;

	/** Find an edge for Other (mutable). Null if absent. */
	FSimAg_Affinity* FindEdge(const FSeam_EntityId& Other);

	/** Find an edge for Other (const). Null if absent. */
	const FSimAg_Affinity* FindEdge(const FSeam_EntityId& Other) const;

	/** Stable id of the owning agent (off a sibling agent component); invalid if none. */
	FSeam_EntityId ResolveAgentId() const;

	/**
	 * Re-resolve the relationship-read seam by casting the Entity relationship world subsystem to the raw
	 * interface. Returns null when the index is absent (Entity module not present / no index). NEVER
	 * stored as a UPROPERTY — re-resolved per use.
	 */
	const ISeam_EntityRelationshipRead* ResolveRelationshipRead() const;

	/** Authority: seed affinities from the relationship index per SeedLinkKind / SeedAffinity. */
	void SeedFromRelationships();

	/** Mark an edge dirty and fire the local delegate; optionally emit the social bus event. */
	void MarkEdgeDirty(FSimAg_Affinity& Edge);

	/** Broadcast a FSimAg_SocialEvent for an interaction with Other. */
	void EmitSocialEvent(const FSeam_EntityId& Other, float Delta) const;
};
