// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SimAg_SkillComponent.generated.h"

/** Fired (server and clients) when this agent's skill set or levels change. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimAg_OnSkillsChanged, USimAg_SkillComponent*, SkillComponent);

/**
 * One skill the agent has, with a proficiency level. Plain replicable members (rides a replicated array).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_SkillLevel
{
	GENERATED_BODY()

	/** The skill tag (child of a project's Job.Skill.* root). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimAgents|Jobs")
	FGameplayTag Skill;

	/** Proficiency level (>= 0). Designer-defined scale; higher = better. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimAgents|Jobs", meta = (ClampMin = "0.0"))
	float Level = 1.f;

	FSimAg_SkillLevel() = default;
	FSimAg_SkillLevel(const FGameplayTag& InSkill, float InLevel) : Skill(InSkill), Level(InLevel) {}
};

/**
 * Advertises an agent's skills/capabilities so the job-claim path can honour a posting's RequiredSkill by
 * CAPABILITY without the board needing to know about agents. A chained-job / haul strategy consults this
 * before committing to a step.
 *
 * REPLICATION: Skills (a tag container) and SkillLevels (a small array) are plain Replicated with OnRep_
 * (they change rarely — gain/level-up — so a fast array is overkill). Every mutator guards authority at
 * the TOP. This component holds no fast-array net state; the values delta-replicate via the array
 * property's own diffing.
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_SkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimAg_SkillComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * True if the agent advertises RequiredSkill (or any child of it). An empty/invalid RequiredSkill is
	 * "no requirement" and always returns true. Client-safe read of replicated state.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Jobs")
	bool HasSkill(FGameplayTag RequiredSkill) const;

	/** Proficiency level for Skill (exact tag), or 0 if the agent lacks it. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Jobs")
	float GetSkillLevel(FGameplayTag Skill) const;

	/** Grant (or raise to) a skill at Level. AUTHORITY ONLY: early-returns on clients. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Jobs")
	void GrantSkill(FGameplayTag Skill, float Level);

	/** Remove a skill entirely. AUTHORITY ONLY. @return true if it was present. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Jobs")
	bool RevokeSkill(FGameplayTag Skill);

	/** The full set of skill tags the agent advertises (client-safe). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Jobs")
	FGameplayTagContainer GetSkills() const { return Skills; }

	/** Fired when skills/levels change (server / client replication). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Jobs")
	FSimAg_OnSkillsChanged OnSkillsChanged;

protected:
	/** Skills (and levels) this component starts with. Seeded into the replicated state on BeginPlay
	 *  (authority only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	TArray<FSimAg_SkillLevel> DefaultSkills;

private:
	/** Replicated set of skill tags (fast membership tests for HasSkill / hierarchy matching). */
	UPROPERTY(ReplicatedUsing = OnRep_Skills)
	FGameplayTagContainer Skills;

	/** Replicated per-skill proficiency levels (parallel to Skills' membership). */
	UPROPERTY(ReplicatedUsing = OnRep_Skills)
	TArray<FSimAg_SkillLevel> SkillLevels;

	/** OnRep for the replicated skill state: fire OnSkillsChanged on clients. */
	UFUNCTION()
	void OnRep_Skills();

	/** Find a level entry by exact skill tag (mutable). Null if absent. */
	FSimAg_SkillLevel* FindLevel(const FGameplayTag& Skill);
};
