// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "DPUILayoutSubsystem.generated.h"

class UDP_LayerStack;
class UDP_ViewBase;

/**
 * Local-player-scoped owner of named UI layer stacks.
 *
 * Each local player gets its own set of layers (so split-screen players have
 * independent UI). The layout subsystem is the only owner of UDP_LayerStack
 * objects; the game-instance-wide UDP_UIManagerSubsystem routes screen requests
 * to the correct local player's layout subsystem and then to the right layer.
 *
 * Layers are created lazily on first access and keyed by GameplayTag
 * (e.g. DP.UI.Layer.HUD, DP.UI.Layer.Menu, DP.UI.Layer.Modal).
 */
UCLASS()
class DESIGNPATTERNSUI_API UDP_UILayoutSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Get (creating if needed) the layer stack for LayerTag.
	 * @return The layer stack, or null if LayerTag is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI")
	UDP_LayerStack* GetOrCreateLayer(FGameplayTag LayerTag);

	/** Get the existing layer stack for LayerTag, or null if it has never been created. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI")
	UDP_LayerStack* FindLayer(FGameplayTag LayerTag) const;

	/** All layer tags that currently have a stack. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI")
	void GetActiveLayers(TArray<FGameplayTag>& OutLayers) const;

	/** Clear (and remove from viewport) every widget on every layer for this local player. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI")
	void ClearAllLayers();

	/** Append a multi-line dump of every layer + its stack to OutLines. */
	void DumpTo(TArray<FString>& OutLines) const;

private:
	/** Named layer stacks for this local player, keyed by layer tag. */
	UPROPERTY()
	TMap<FGameplayTag, TObjectPtr<UDP_LayerStack>> Layers;
};
