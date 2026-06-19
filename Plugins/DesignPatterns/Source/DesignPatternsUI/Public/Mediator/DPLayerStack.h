// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "DPLayerStack.generated.h"

class UDP_ViewBase;
class UUserWidget;

/**
 * One entry on a layer stack: the realised widget plus the screen tag that
 * produced it, so the stack can be queried/dumped by tag.
 */
USTRUCT()
struct DESIGNPATTERNSUI_API FDP_LayerStackEntry
{
	GENERATED_BODY()

	/** The screen tag that this widget instance represents. */
	UPROPERTY()
	FGameplayTag ScreenTag;

	/** The live widget instance. Owning ref keeps it alive while on the stack. */
	UPROPERTY()
	TObjectPtr<UDP_ViewBase> Widget = nullptr;

	/** ZOrder the widget was added to the viewport with. */
	UPROPERTY()
	int32 ZOrder = 0;
};

/**
 * A push/pop stack of view widgets for a SINGLE named UI layer
 * (e.g. "Menu", "HUD", "Modal").
 *
 * The stack owns the lifetime of the widgets it contains: pushing adds a widget
 * to the viewport and the stack; popping removes the top widget from the viewport
 * and releases it. Only the top widget is "active"; lower entries remain in the
 * viewport (so backgrounds show through) unless the owner chooses to hide them.
 *
 * This object is a plain UObject created/owned by UDP_UILayoutSubsystem — it is
 * not a singleton and is per-layer, per-local-player.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSUI_API UDP_LayerStack : public UObject
{
	GENERATED_BODY()

public:
	/** Initialise with the layer this stack represents. Called by the layout subsystem. */
	void InitLayer(FGameplayTag InLayerTag);

	/** The layer tag this stack manages. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI")
	FGameplayTag GetLayerTag() const { return LayerTag; }

	/**
	 * Push an already-created widget onto this layer and add it to the viewport.
	 * The stack takes ownership of the widget's lifetime.
	 *
	 * @param ScreenTag  Identity tag of the screen (for later lookup/pop-by-tag).
	 * @param Widget     The created (but not-yet-added) view widget.
	 * @param ZOrder     Viewport ZOrder for AddToViewport.
	 * @return true if pushed.
	 */
	bool Push(FGameplayTag ScreenTag, UDP_ViewBase* Widget, int32 ZOrder);

	/**
	 * Pop the top widget: remove it from the viewport and release it.
	 * @return true if a widget was popped (stack was non-empty).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI")
	bool Pop();

	/**
	 * Pop down to (and excluding) the first entry matching ScreenTag from the top,
	 * then pop that entry too. No-op if the tag is not present.
	 * @return true if any entry was popped.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI")
	bool PopToTag(FGameplayTag ScreenTag);

	/** Remove and release every widget on this layer. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI")
	void Clear();

	/** The top (active) widget, or null if empty. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI")
	UDP_ViewBase* GetTopWidget() const;

	/** The screen tag of the top entry, or an empty tag if the stack is empty. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI")
	FGameplayTag GetTopScreenTag() const;

	/** Number of widgets currently on this layer. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI")
	int32 Num() const { return Entries.Num(); }

	/** True if a widget for ScreenTag is anywhere on this layer. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI")
	bool ContainsScreen(FGameplayTag ScreenTag) const;

	/** Append a human-readable dump of this layer's contents (top first) to OutLines. */
	void DumpTo(TArray<FString>& OutLines) const;

private:
	/** Remove Entry's widget from the viewport (if added) — shared teardown. */
	void RemoveEntryWidget(const FDP_LayerStackEntry& Entry);

	/** The layer identity. */
	UPROPERTY()
	FGameplayTag LayerTag;

	/** Stack contents, index 0 = bottom, last = top. */
	UPROPERTY()
	TArray<FDP_LayerStackEntry> Entries;
};
