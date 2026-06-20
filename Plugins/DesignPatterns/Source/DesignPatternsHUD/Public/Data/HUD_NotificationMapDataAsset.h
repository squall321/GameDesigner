// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Notification/HUD_NotificationTypes.h"
#include "HUD_NotificationMapDataAsset.generated.h"

/**
 * One mapping rule: when a message is observed on BusChannel, surface a notification built from
 * Template. Data-driven so designers wire gameplay events to toasts/banners with NO code change.
 *
 * The bus channel is matched hierarchy-aware (a rule on DP.Bus.Combat fires for
 * DP.Bus.Combat.Damage). When several rules match a single message, the most-specific (deepest)
 * channel wins; ties keep the earlier-authored rule.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_NotificationMapEntry
{
	GENERATED_BODY()

	/**
	 * The bus channel that triggers this rule (e.g. DP.Bus.Combat.LevelUp). Matched against the
	 * broadcast channel hierarchy-aware.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Notification",
		meta = (Categories = "DP.Bus"))
	FGameplayTag BusChannel;

	/**
	 * The notification template instantiated when this rule fires. Title/Body are FText so they are
	 * localizable; the subsystem copies the template verbatim (producers that need data interpolation
	 * use the direct push API or the bus FHUD_NotificationBusPayload path instead).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Notification")
	FHUD_Notification Template;

	/** True if this rule has a usable channel and content. */
	bool IsValidRule() const
	{
		return BusChannel.IsValid() && Template.HasContent();
	}
};

/**
 * Data asset mapping bus channels to notification templates.
 *
 * Consumed by UHUD_NotificationSubsystem: it reads the rules' distinct BusChannels, subscribes once
 * per channel on the message bus, and on a matching broadcast enqueues the mapped notification. This
 * keeps gameplay producers (which only broadcast DP.Bus.* events) fully decoupled from the HUD —
 * the wiring is pure data.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Notification Map"))
class DESIGNPATTERNSHUD_API UHUD_NotificationMapDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Every bus-channel -> notification rule. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Notification",
		meta = (TitleProperty = "BusChannel"))
	TArray<FHUD_NotificationMapEntry> Entries;

	/** The distinct, valid bus channels referenced by the rules (de-duplicated). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "HUD|Notification")
	void GetSubscribedChannels(TArray<FGameplayTag>& OutChannels) const;

	/**
	 * Resolve the best-matching rule for a broadcast channel: the deepest BusChannel that the
	 * broadcast matches (hierarchy-aware), earliest-authored on ties.
	 * @return Pointer into Entries, or null if no rule matches.
	 */
	const FHUD_NotificationMapEntry* FindBestRule(const FGameplayTag& BroadcastChannel) const;

	//~ Begin UDP_DataAsset
	/** Collapses every HUD notification-map subclass into one asset-manager bucket ("HUD_NotificationMap"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags rules missing a channel or content. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
