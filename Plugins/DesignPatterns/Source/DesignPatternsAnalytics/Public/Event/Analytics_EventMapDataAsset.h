// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Analytics/Seam_AnalyticsSink.h"
#include "Analytics_EventMapDataAsset.generated.h"

class UDP_MessageBusSubsystem;

/**
 * How a single attribute is extracted from an observed bus message and emitted on the
 * resulting analytics event.
 *
 * The bus payload is an FInstancedStruct, but analytics attributes are PII-safe FSeam_NetValue
 * (the closed bool/int/float/vector/tag/name union) by construction. This rule therefore
 * supports two value sources, BOTH of which are structurally incapable of carrying PII:
 *  - A literal FSeam_NetValue baked into the data asset (e.g. a constant "platform" tag), or
 *  - A "channel" pseudo-source that emits the message's own channel tag as a Tag value.
 *
 * We deliberately do NOT reflectively pull arbitrary fields out of the payload struct: doing
 * so could surface free-form strings/ids and defeat the PII guarantee. Projects that need a
 * value off the payload publish it as part of a purpose-built, PII-free payload and map it
 * with a literal/derived rule, or extend this asset in their own subclass.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSANALYTICS_API FAnalytics_AttrExtractRule
{
	GENERATED_BODY()

	/** Attribute key written onto the analytics event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Analytics")
	FName AttributeKey;

	/**
	 * When true, the rule emits the source bus channel tag as the attribute value (a Tag
	 * FSeam_NetValue). When false, the LiteralValue below is used verbatim.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Analytics")
	bool bUseSourceChannelAsValue = false;

	/** Constant value emitted when bUseSourceChannelAsValue is false. PII-safe by type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Analytics", meta = (EditCondition = "!bUseSourceChannelAsValue"))
	FSeam_NetValue LiteralValue;
};

/**
 * One bus-channel -> analytics-event conversion entry.
 *
 * When a message arrives on an observed channel that exactly matches (or is a child of)
 * SourceBusChannel, the subsystem records AnalyticsEvent with the configured attributes.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSANALYTICS_API FAnalytics_EventMapEntry
{
	GENERATED_BODY()

	/** Bus channel to convert. Child channels also match unless a more specific entry exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Analytics")
	FGameplayTag SourceBusChannel;

	/** Analytics event id emitted for this channel (anchor under Analytics.Event.*). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Analytics")
	FGameplayTag AnalyticsEvent;

	/**
	 * If true, the original bus channel tag is automatically added as a "channel" Tag
	 * attribute on the emitted event (handy for disambiguating child channels that collapse
	 * onto one analytics event). The extra attributes below are appended after it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Analytics")
	bool bIncludeSourceChannelAttribute = true;

	/** Additional PII-safe attributes to emit on the event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Analytics")
	TArray<FAnalytics_AttrExtractRule> Attributes;
};

/**
 * Data-driven map from bus channels to analytics events.
 *
 * This is the seam between "what the game broadcasts" and "what we measure": designers edit
 * this asset to decide which DP.Bus.* signals become telemetry and with which (PII-safe)
 * attributes — no code changes, no recompiles. The Analytics subsystem loads the asset named
 * in Analytics_DeveloperSettings::DefaultEventMap (or one supplied at runtime) and consults it
 * for every observed bus message.
 *
 * Identity is the inherited DataTag, so the asset can also be resolved via the data registry.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSANALYTICS_API UAnalytics_EventMapDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The conversion table. Order matters only for tie-breaking (see ResolveEntryForChannel). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Analytics")
	TArray<FAnalytics_EventMapEntry> Entries;

	/**
	 * Find the best-matching entry for an incoming bus channel.
	 *
	 * Matching prefers the MOST SPECIFIC entry: an exact tag match wins over a parent-channel
	 * match, and among parent matches the deepest (longest) source tag wins. Returns nullptr if
	 * no entry matches. The returned pointer is owned by this asset and valid for its lifetime.
	 */
	const FAnalytics_EventMapEntry* ResolveEntryForChannel(const FGameplayTag& Channel) const;

	/**
	 * Build the analytics attribute list for an entry given the source channel. Applies the
	 * source-channel attribute and the literal/derived rules. All produced values are
	 * FSeam_NetValue and therefore PII-safe by construction.
	 */
	void BuildAttributes(
		const FAnalytics_EventMapEntry& Entry,
		const FGameplayTag& SourceChannel,
		TArray<FSeam_AnalyticsAttr>& OutAttrs) const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Validates that every entry has a source channel and a valid analytics event id. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
