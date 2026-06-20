// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "SimAg_Agent.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimAg_Agent : public UInterface
{
	GENERATED_BODY()
};

/**
 * Agent seam: the small read-only surface other systems use to ask "what is this agent and what is it
 * doing?" without depending on the concrete USimAg_AgentComponent.
 *
 * Strategies, steering, UI and sibling modules resolve this off the owning actor and read the agent's
 * tag, current activity, and home/work anchor locations. Keeping it an interface lets a squad
 * coordinator or a scripted NPC stand in for a full agent component.
 */
class DESIGNPATTERNSSIMAGENTS_API ISimAg_Agent
{
	GENERATED_BODY()

public:
	/** The agent's archetype/role tag (e.g. "SimAg.Agent.Villager"). Empty if unassigned. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Agent")
	FGameplayTag GetAgentTag() const;

	/** The activity the agent is currently performing (its replicated CurrentActivity). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Agent")
	FGameplayTag GetCurrentActivity() const;

	/** The agent's home anchor in world space (where it sleeps / idles). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Agent")
	FVector GetHomeLocation() const;

	/** The agent's work anchor in world space (where its job/schedule sends it). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Agent")
	FVector GetWorkLocation() const;
};
