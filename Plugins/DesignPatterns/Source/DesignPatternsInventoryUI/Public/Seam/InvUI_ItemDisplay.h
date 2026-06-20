// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Engine/Texture2D.h"
#include "InvUI_ItemDisplay.generated.h"

/**
 * Resolved, ready-to-display presentation for one item tag. Produced asynchronously by an
 * IInvUI_ItemDisplay resolver and handed to a viewmodel slot. The icon is delivered as a soft
 * pointer so the viewmodel owns the async load lifetime (streaming + cancellation) rather than
 * forcing the resolver to keep textures resident.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_ItemDisplayInfo
{
	GENERATED_BODY()

	/** The item this info describes (echoed back for async correlation). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Display")
	FGameplayTag ItemTag;

	/** Localized display name. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Display")
	FText DisplayName;

	/** Localized short description / tooltip body. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Display")
	FText Description;

	/** Soft pointer to the item's icon; the consumer streams it in on demand. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Display")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Optional rarity / quality colour the UI may tint the slot frame with. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Display")
	FLinearColor QualityColor = FLinearColor::White;

	/** True when this info was successfully resolved (false = unknown item / resolve failed). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Display")
	bool bResolved = false;
};

/** Async result delegate: fired once per ResolveItemDisplay call with the populated info. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FInvUI_OnItemDisplayResolved, const FInvUI_ItemDisplayInfo&, Info);

UINTERFACE(MinimalAPI, BlueprintType, meta = (DisplayName = "InvUI Item Display"))
class UInvUI_ItemDisplay : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam that turns an opaque item tag into presentable display info (name/description/icon/
 * quality colour), asynchronously. A genre module (or the project) implements this — typically
 * by looking the tag up in the core data registry and reading a display data asset — and
 * publishes it under InvUITags::Service_ItemDisplay so any inventory window can resolve icons
 * without knowing where item metadata lives.
 *
 * Resolution is async by contract: implementations may need to load a soft display asset.
 * The synchronous TryGetCached path lets the viewmodel paint instantly when the info is
 * already resident and fall back to the async path otherwise.
 */
class IInvUI_ItemDisplay
{
	GENERATED_BODY()

public:
	/**
	 * Begin resolving display info for ItemTag. OnResolved is invoked exactly once — possibly
	 * synchronously, possibly on a later frame after a load completes. Implementations must
	 * still fire OnResolved (with bResolved=false) for an unknown tag so callers can clear
	 * pending state.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Display")
	void ResolveItemDisplay(FGameplayTag ItemTag, const FInvUI_OnItemDisplayResolved& OnResolved);

	/**
	 * Synchronous fast path: if display info for ItemTag is already cached/resident, fill
	 * OutInfo and return true. Returns false (and leaves OutInfo default) when a resolve would
	 * have to load — the caller should then use the async ResolveItemDisplay path.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Display")
	bool TryGetCachedDisplay(FGameplayTag ItemTag, FInvUI_ItemDisplayInfo& OutInfo) const;
};
