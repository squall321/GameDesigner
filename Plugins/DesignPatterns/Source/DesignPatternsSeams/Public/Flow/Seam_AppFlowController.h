// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_AppFlowController.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_AppFlowController : public UInterface
{
	GENERATED_BODY()
};

/**
 * App-level flow seam (Boot/Title/MainMenu/Lobby/Loading/InGame/Pause/Results). The GameFlow subsystem
 * implements it; tutorial, AI directors and save-slot UI read/drive the top-level phase through it
 * without depending on the GameFlow module. Phases are tag-keyed so a game extends the set freely.
 */
class DESIGNPATTERNSSEAMS_API ISeam_AppFlowController
{
	GENERATED_BODY()

public:
	/** The current top-level flow phase tag. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Flow")
	FGameplayTag GetActivePhase() const;

	/** Request a transition to PhaseTag. Returns false if the transition is not currently allowed. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Flow")
	bool RequestTransition(FGameplayTag PhaseTag);

	/** True if a transition to PhaseTag is currently permitted (without performing it). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Flow")
	bool CanEnterPhase(FGameplayTag PhaseTag) const;
};
