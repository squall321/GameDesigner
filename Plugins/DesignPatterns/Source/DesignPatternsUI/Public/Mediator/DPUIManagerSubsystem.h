// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4 and is merged into
// CoreUObject in 5.5+. Include the right header for the engine band.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "DPUIManagerSubsystem.generated.h"

class UDP_UIRegistryDataAsset;
class UDP_UILayoutSubsystem;
class UDP_LayerStack;
class UDP_ViewBase;
class UDP_ViewModelBase;
class ULocalPlayer;
struct FDP_ScreenDef;

/**
 * The UI mediator / router (GameInstance-scoped).
 *
 * Call sites only ever speak in tags: "push the screen DP.UI.Screen.Pause".
 * The manager resolves the tag against a UDP_UIRegistryDataAsset to get the
 * widget class + target layer, creates the widget, optionally assigns a
 * ViewModel, and pushes it onto the correct layer of the targeted local player's
 * UDP_UILayoutSubsystem. Views and gameplay never reference each other directly —
 * everything flows through this mediator and the core message bus.
 *
 * This subsystem also backs the DP.UI.* console commands (DumpStack, etc.).
 */
UCLASS()
class DESIGNPATTERNSUI_API UDP_UIManagerSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/**
	 * Set the active screen registry. Normally read from developer settings on
	 * Initialize, but exposed so a game can swap registries at runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI")
	void SetRegistry(UDP_UIRegistryDataAsset* InRegistry);

	/** The active registry, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI")
	UDP_UIRegistryDataAsset* GetRegistry() const { return Registry; }

	/**
	 * Resolve ScreenTag through the registry, create its widget and push it onto
	 * the screen's layer for the given (or first) local player.
	 *
	 * @param ScreenTag    Registry key of the screen to show.
	 * @param ViewModel    Optional ViewModel to assign to the created view.
	 * @param LocalPlayer  Target local player; null uses the first local player.
	 * @return The created view, or null on failure (unknown tag / load failure).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI",
		meta = (AdvancedDisplay = "ViewModel,LocalPlayer"))
	UDP_ViewBase* PushScreen(FGameplayTag ScreenTag, UDP_ViewModelBase* ViewModel = nullptr,
		ULocalPlayer* LocalPlayer = nullptr);

	/**
	 * Pop the top widget from the given layer for the targeted local player.
	 * @return true if a widget was popped.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI",
		meta = (AdvancedDisplay = "LocalPlayer"))
	bool PopScreen(FGameplayTag LayerTag, ULocalPlayer* LocalPlayer = nullptr);

	/**
	 * Pop down to and including the screen identified by ScreenTag on its layer.
	 * @return true if anything was popped.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI",
		meta = (AdvancedDisplay = "LocalPlayer"))
	bool PopToScreen(FGameplayTag ScreenTag, ULocalPlayer* LocalPlayer = nullptr);

	/** The screen tag currently on top of LayerTag, or an empty tag. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI",
		meta = (AdvancedDisplay = "LocalPlayer"))
	FGameplayTag GetTopScreen(FGameplayTag LayerTag, ULocalPlayer* LocalPlayer = nullptr) const;

	/** Build a multi-line dump of every local player's layers/stacks. */
	FString BuildStackDump() const;

	/** Log BuildStackDump() — backing for the DP.UI.DumpStack console command. */
	void DumpStackToLog() const;

private:
	/** Resolve the layout subsystem for the given (or first) local player. */
	UDP_UILayoutSubsystem* GetLayoutFor(ULocalPlayer* LocalPlayer) const;

	/** First local player of this game instance, or null. */
	ULocalPlayer* GetFirstLocalPlayer() const;

	/** Synchronously load + create a view widget for ScreenDef under OwningPlayer. */
	UDP_ViewBase* CreateViewForScreen(const FDP_ScreenDef& ScreenDef, ULocalPlayer* OwningPlayer);

	/** The active screen registry. */
	UPROPERTY()
	TObjectPtr<UDP_UIRegistryDataAsset> Registry = nullptr;

	/** Registered console commands (DP.UI.*); cleaned up on Deinitialize. */
	TArray<IConsoleObject*> ConsoleCommands;
};
