// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "View/DPViewBase.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "DPInputPromptWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

/**
 * A widget that shows the correct input glyph + label for a gameplay action on the player's CURRENT
 * input device (keyboard/mouse, gamepad family, touch).
 *
 * It resolves the shared ISeam_InputGlyphProvider from the service locator under
 * DP.Service.Platform.Glyphs (held weakly, no-op when unset) so it draws the right button icon
 * without depending on the Platform module. The glyph texture crosses the seam as a TSoftObjectPtr,
 * so this widget async-loads it via the streamable manager rather than force-loading an atlas.
 *
 * It refreshes automatically when the input device changes: it listens on the
 * DPUITags::Bus_InputDeviceChanged channel and re-resolves the glyph for the new device family.
 *
 * Built on UDP_ViewBase so it can additionally be MVVM-bound if a screen wants to drive the action
 * tag from a ViewModel; the simple path is just to set ActionTag in the designer.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "DP Input Prompt"))
class DESIGNPATTERNSUI_API UDP_InputPromptWidget : public UDP_ViewBase
{
	GENERATED_BODY()

public:
	/** Set the action this prompt represents and refresh the glyph immediately. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Prompt")
	void SetActionTag(FGameplayTag InActionTag);

	/** The action this prompt currently shows. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Prompt")
	FGameplayTag GetActionTag() const { return ActionTag; }

	/** Re-resolve and re-apply the glyph/label for the current action and device. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Prompt")
	void RefreshGlyph();

protected:
	//~ Begin UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget

	/** The action whose binding glyph is shown. Authored per prompt (data, not code). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Prompt")
	FGameplayTag ActionTag;

	/** Optional bound image that receives the resolved glyph texture. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "DesignPatterns|UI|Prompt")
	TObjectPtr<UImage> GlyphImage = nullptr;

	/** Optional bound text block that receives the resolved binding label (fallback when no glyph). */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "DesignPatterns|UI|Prompt")
	TObjectPtr<UTextBlock> LabelText = nullptr;

	/**
	 * Designer hook fired after a glyph/label has been resolved & applied, so a BP can do extra
	 * styling (e.g. show the label only when no glyph texture resolved).
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DesignPatterns|UI|Prompt",
		meta = (DisplayName = "On Glyph Resolved"))
	void OnGlyphResolved(bool bHasGlyph, const FText& Label);

private:
	/** Resolve the input-glyph provider weakly from the locator, or an empty interface. */
	class UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** Subscribe to the device-changed bus channel so the prompt refreshes on device swap. */
	void SubscribeDeviceChanged();

	/** Async-load callback: apply the loaded glyph texture to the image. */
	void OnGlyphTextureLoaded();

	/** Apply Label to LabelText and (after async load) Texture to GlyphImage. */
	void ApplyResolved(bool bHasGlyph, const FText& Label);

	/** Soft pointer currently being / last resolved, so the async callback applies the right one. */
	TSoftObjectPtr<UTexture2D> PendingGlyph;

	/** The label resolved alongside PendingGlyph, applied with the texture. */
	UPROPERTY()
	FText PendingLabel;

	/** Bus listener handle for device-change refresh; removed in NativeDestruct. */
	FDP_ListenerHandle DeviceChangedHandle;
};
