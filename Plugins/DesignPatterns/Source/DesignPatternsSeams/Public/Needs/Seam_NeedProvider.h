// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_NeedProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_NeedProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Shared "need" seam (hunger, energy, social, fun, ...), tag-keyed so any genre defines its own needs.
 * ONE definition prevents the "two needs systems" collision between Survival meters and SimAgents
 * utility needs. A consumer (e.g. the agent brain) can compose MULTIPLE providers: it asks each whether
 * it SupportsNeed(Tag) and reads the normalized value only from the provider that owns that need — so a
 * Survival needs adapter (hunger/thirst/...) and a sim-only needs component (social/fun) coexist.
 */
class DESIGNPATTERNSSEAMS_API ISeam_NeedProvider
{
	GENERATED_BODY()

public:
	/** Normalized [0,1] satisfaction of NeedTag (1 = fully satisfied). 0 if unsupported. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Needs")
	float GetNeedNormalized(FGameplayTag NeedTag) const;

	/** True if this provider owns/answers NeedTag. Lets a brain compose several providers. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Needs")
	bool SupportsNeed(FGameplayTag NeedTag) const;

	/** Append every need tag this provider answers (for enumeration / UI). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Needs")
	void GetSupportedNeeds(FGameplayTagContainer& OutNeeds) const;
};
