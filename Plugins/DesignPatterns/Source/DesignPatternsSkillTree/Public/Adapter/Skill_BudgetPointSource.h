// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Seam/Skill_PointSource.h"
#include "Skill_BudgetPointSource.generated.h"

/**
 * One replicated per-channel point budget entry. A character may earn skill points on several channels
 * (e.g. a combat channel and a crafting channel); each channel's running total is one entry here.
 * Replicated as a fast-array item so a single channel's change delta-replicates rather than the whole map.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSKILLTREE_API FSkill_PointBudgetEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The point channel this budget is for (empty = the default channel). */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree|Points")
	FGameplayTag ChannelTag;

	/** Total points granted on this channel over the character's lifetime. */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree|Points")
	int32 TotalGranted = 0;

	FSkill_PointBudgetEntry() = default;

	/** Convenience constructor. */
	FSkill_PointBudgetEntry(const FGameplayTag& InChannel, int32 InTotal)
		: ChannelTag(InChannel), TotalGranted(InTotal) {}
};

/** Fast-array of per-channel point budgets. Delta-replicated; no client-side callbacks needed (reads only). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSKILLTREE_API FSkill_PointBudgetArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** The replicated per-channel budget entries. */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree|Points")
	TArray<FSkill_PointBudgetEntry> Entries;

	/** Delta-serialize only the changed channels. */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSkill_PointBudgetEntry, FSkill_PointBudgetArray>(Entries, DeltaParms, *this);
	}

	/** Find a channel entry (mutable), or null. */
	FSkill_PointBudgetEntry* FindByChannel(const FGameplayTag& InChannel)
	{
		for (FSkill_PointBudgetEntry& Entry : Entries)
		{
			if (Entry.ChannelTag == InChannel)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	/** Sum the budget for a channel (const). 0 if absent. */
	int32 GetTotalForChannel(const FGameplayTag& InChannel) const
	{
		for (const FSkill_PointBudgetEntry& Entry : Entries)
		{
			if (Entry.ChannelTag == InChannel)
			{
				return Entry.TotalGranted;
			}
		}
		return 0;
	}
};

/** Enables NetDeltaSerialize for the point-budget array. */
template<>
struct TStructOpsTypeTraits<FSkill_PointBudgetArray> : public TStructOpsTypeTraitsBase2<FSkill_PointBudgetArray>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * Designer/event-driven skill-point budget, implementing the ISkill_PointSource seam.
 *
 * This is the "where do points come from" side of the skill system: gameplay (level-up rewards, quest
 * payouts, debug grants) calls GrantPoints(Channel, Amount) on the AUTHORITY to add points to a channel,
 * and USkill_SkillComponent resolves this adapter to read the total budget for its configured channel.
 * The per-channel totals replicate (fast-array on this UActorComponent) so client UI can show the earned
 * total; spending is tracked separately by the skill component (TotalEarned - spent = available).
 *
 * GrantPoints guards HasAuthority() at the top and early-returns on clients. Optionally seed a starting
 * budget per channel via StartingBudgets for trees that begin with points already earned.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSKILLTREE_API USkill_BudgetPointSource : public UActorComponent, public ISkill_PointSource
{
	GENERATED_BODY()

public:
	USkill_BudgetPointSource();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void InitializeComponent() override;
	//~ End UActorComponent

	//~ Begin ISkill_PointSource
	virtual int32 GetTotalEarnedPoints_Implementation(FGameplayTag Channel) const override;
	virtual int32 GetOwnerLevel_Implementation() const override;
	//~ End ISkill_PointSource

	/**
	 * Add Amount points to a channel's lifetime budget. AUTHORITY ONLY: guards HasAuthority() at the top and
	 * early-returns on clients. Amount must be > 0 (negative grants are rejected — revocation isn't supported
	 * here; use a respec on the skill component instead). Replicates the updated channel total.
	 * @return the channel's new total budget (or the unchanged total if the call was rejected/non-authority).
	 */
	UFUNCTION(BlueprintCallable, Category = "SkillTree|Points")
	int32 GrantPoints(FGameplayTag ChannelTag, int32 Amount);

	/** Read the lifetime budget for a channel (local; valid on clients via replication). */
	UFUNCTION(BlueprintPure, Category = "SkillTree|Points")
	int32 GetChannelBudget(FGameplayTag ChannelTag) const;

	/**
	 * Set the owner level reported through the seam. AUTHORITY ONLY: guards HasAuthority() at the top and
	 * early-returns on clients. Replicates the new level. Clamped non-negative.
	 */
	UFUNCTION(BlueprintCallable, Category = "SkillTree|Points")
	void SetOwnerLevel(int32 InLevel);

	/**
	 * The channel a caller gets when it queries with an invalid (empty) channel tag. Designer-set; empty
	 * means a single anonymous channel keyed by the empty tag, which is valid. The ISkill_PointSource
	 * contract maps an invalid Channel argument onto this default before reading the per-channel budget.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SkillTree|Points")
	FGameplayTag DefaultChannelTag;

	/**
	 * The owner level reported through ISkill_PointSource::GetOwnerLevel for level-gated skill nodes. This is
	 * the designer/event-driven budget source, so the level is a simple replicated scalar set via SetOwnerLevel
	 * on authority. 0 means "ungated" (the seam's documented inert default). A project that derives level from
	 * an XP/stats system should implement ISkill_PointSource there instead and not place this adapter.
	 * Replicated so clients read the same gated level the server evaluates against.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "SkillTree|Points", meta = (ClampMin = "0"))
	int32 OwnerLevel = 0;

	/** Starting per-channel budgets seeded on authority at InitializeComponent (e.g. a tutorial freebie). */
	UPROPERTY(EditAnywhere, Category = "SkillTree|Points")
	TMap<FGameplayTag, int32> StartingBudgets;

private:
	/** Replicated per-channel lifetime budgets. */
	UPROPERTY(Replicated)
	FSkill_PointBudgetArray Budgets;

	/** True on the network authority; GrantPoints/seeding guard on this. */
	bool HasAuthorityToMutate() const;
};
