// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "Flow_BootSequenceDefinition.generated.h"

class UFlow_BootStepDefinition;

/**
 * Ordered list of boot steps the boot controller runs while the FSM sits in Flow.Phase.Boot. The whole
 * boot policy (which legal screens, what to preload, when to load the profile, the first-run flow) is
 * authored here as data; the controller holds none of it inline.
 *
 * Identity is the inherited DataTag (e.g. Flow.Data.BootSequence.Default). The Flow settings reference a
 * sequence by soft ref; the controller iterates Steps in order.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Flow Boot Sequence Definition"))
class DESIGNPATTERNSGAMEFLOW_API UFlow_BootSequenceDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The boot steps, executed in array order. Each is a hard ref (a boot sequence is tiny and resolved
	 * once at startup; the heavy content the steps preload is soft inside each step).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot")
	TArray<TObjectPtr<UFlow_BootStepDefinition>> Steps;
};
