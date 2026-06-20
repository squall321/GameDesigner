// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "SimAg_ScheduleAsset.generated.h"

/**
 * One entry in a daily schedule: from StartHour onward (until the next entry's StartHour), the agent
 * should be performing Activity, ideally at Location. Both are FGameplayTags so any genre defines its
 * own activities/locations as children of SimAgNativeTags::Activity / ::Location.
 *
 * Entries are interpreted as a wrapping timeline over a single day: the entry with the largest
 * StartHour <= the current hour wins, and if the current hour is before the first entry's StartHour
 * the LAST entry of the day applies (it wraps past midnight). Hours are in [0, HoursPerDay).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_ScheduleEntry
{
	GENERATED_BODY()

	/** Hour-of-day this entry becomes active, in [0, HoursPerDay). Fractional hours allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Schedule", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "24.0"))
	float StartHour = 0.f;

	/** What the agent does during this slot (child of SimAg.Activity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Schedule", meta = (Categories = "SimAg.Activity"))
	FGameplayTag Activity;

	/** Where the agent should be for this slot (child of SimAg.Location). Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Schedule", meta = (Categories = "SimAg.Location"))
	FGameplayTag Location;

	FSimAg_ScheduleEntry() = default;
	FSimAg_ScheduleEntry(float InStartHour, const FGameplayTag& InActivity, const FGameplayTag& InLocation)
		: StartHour(InStartHour), Activity(InActivity), Location(InLocation) {}

	/** Sort predicate by StartHour, used to keep the timeline ordered for resolution. */
	bool operator<(const FSimAg_ScheduleEntry& Other) const { return StartHour < Other.StartHour; }
};

/**
 * Message-bus payload broadcast when an agent's active schedule activity changes. Carried as an
 * FInstancedStruct through UDP_MessageBusSubsystem on channel SimAgNativeTags::Bus_ActivityChanged.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_ActivityEvent
{
	GENERATED_BODY()

	/** The activity now in effect (may be empty if the schedule has no entry). */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Schedule")
	FGameplayTag NewActivity;

	/** The activity that was in effect before the change. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Schedule")
	FGameplayTag PreviousActivity;

	/** The location associated with the new activity. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Schedule")
	FGameplayTag NewLocation;
};

/**
 * A tag-identified daily schedule asset: an ordered list of FSimAg_ScheduleEntry that maps any hour
 * of the day to an activity/location. Authored once as a data asset and shared by many agents (e.g.
 * a "Farmer" schedule). Resolved by tag through the core data registry like any UDP_DataAsset.
 *
 * The asset is genre-neutral: activities and locations are tags, so the same schedule machinery
 * drives a townsperson sim, a colony sim, or a stealth-guard patrol with no code change.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMAGENTS_API USimAg_ScheduleAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimAg_ScheduleAsset();

	/**
	 * The day's entries. Need not be pre-sorted in the editor; ResolveActivityForHour sorts a local
	 * copy. Authoring tip: cover hour 0 so early-morning hours always resolve without wrap ambiguity.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Schedule")
	TArray<FSimAg_ScheduleEntry> Entries;

	/**
	 * Resolve the entry active at HourOfDay (in [0, HoursPerDay)). Picks the entry with the greatest
	 * StartHour <= HourOfDay; if HourOfDay precedes every entry, the last entry wraps from the
	 * previous day. Returns a default (empty) entry if Entries is empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Schedule")
	FSimAg_ScheduleEntry ResolveActivityForHour(float HourOfDay) const;

	//~ Begin UDP_DataAsset
	/** Group all schedule assets under one asset-manager type bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Warns on out-of-range hours and entries with no Activity. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
