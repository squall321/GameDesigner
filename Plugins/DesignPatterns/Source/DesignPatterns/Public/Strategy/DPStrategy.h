// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ScriptInterface.h"
#include "GameFramework/Actor.h"
#include "FSM/DPBlackboard.h"
#include "DPStrategy.generated.h"

/**
 * Read-only context handed to every strategy when it is scored and executed.
 *
 * Holds only non-owning references: the acting actor (weak, so a destroyed pawn cannot keep
 * the context alive) and the blackboard reached through the IDP_BlackboardProvider seam.
 * Passed by const ref so scoring stays allocation-free on the hot path.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_StrategyContext
{
	GENERATED_BODY()

	/** The actor the strategy acts upon / on behalf of. Weak: never keeps the actor alive. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Strategy")
	TWeakObjectPtr<AActor> Owner;

	/** Shared blackboard accessed via the provider seam (no AIModule dependency). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Strategy")
	TScriptInterface<IDP_BlackboardProvider> Blackboard;

	FDP_StrategyContext() = default;

	FDP_StrategyContext(AActor* InOwner, const TScriptInterface<IDP_BlackboardProvider>& InBlackboard)
		: Owner(InOwner)
		, Blackboard(InBlackboard)
	{
	}
};

/**
 * Strategy pattern leaf: an interchangeable, designer-authored unit of behaviour.
 *
 * EditInlineNew + Blueprintable so strategies are authored inline inside a selector and can be
 * subclassed in C++ or Blueprint. ScoreFor produces a comparable fitness used by selectors;
 * Execute performs the action. Abstract — concrete behaviour lives in subclasses.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable, DefaultToInstanced, CollapseCategories)
class DESIGNPATTERNS_API UDP_Strategy : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Rate this strategy's fitness for the given context. Higher is better. Return a value
	 * <= 0 to signal "not applicable". Must be side-effect free so selectors can poll freely.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Strategy")
	float ScoreFor(const FDP_StrategyContext& Context) const;
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const;

	/** Carry out this strategy's behaviour for the given context. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Strategy")
	void Execute(const FDP_StrategyContext& Context);
	virtual void Execute_Implementation(const FDP_StrategyContext& Context);

	/** Designer-facing label for debugging / selector logs. Defaults to the class name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Strategy")
	FName DebugName;

	/** @return DebugName when set, otherwise a trimmed class name. */
	FName GetDebugName() const;

protected:
	/** Convenience: resolve the World from the context owner (may be null). */
	UWorld* GetWorldFromContext(const FDP_StrategyContext& Context) const;
};
