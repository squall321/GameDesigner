// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "InvUI_ContainerCapabilities.generated.h"

UINTERFACE(MinimalAPI, BlueprintType, meta = (DisplayName = "InvUI Container Capabilities"))
class UInvUI_ContainerCapabilities : public UInterface
{
	GENERATED_BODY()
};

/**
 * OPTIONAL display-capability companion to IInvUI_ItemContainer.
 *
 * A backend advertises which InvUITags::Cap_* affordances it supports (Cap_Move / Cap_Split /
 * Cap_Sort / Cap_FixedSlots / Cap_ReadOnly) so the window UI — the context menu and the filter
 * bar — can enable or grey out actions generically, without coupling to the backend's concrete
 * type. This is purely a UI hint (the authoritative server still re-validates every mutation via
 * IInvUI_ContainerAccess); a backend that does not implement it is treated as having no advertised
 * capabilities, so the UI falls back to its own defaults. BlueprintNativeEvent (Execute_ thunks).
 */
class IInvUI_ContainerCapabilities
{
	GENERATED_BODY()

public:
	/** Append every capability tag this container advertises (Cap_*). Default: none. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Capabilities")
	void GetCapabilities(FGameplayTagContainer& Out) const;

	/** True when the container advertises Cap (hierarchy-aware). Default: false. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Capabilities")
	bool HasCapability(FGameplayTag Cap) const;
};
