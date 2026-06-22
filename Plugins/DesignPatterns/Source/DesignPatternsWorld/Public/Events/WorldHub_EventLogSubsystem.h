// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UObject/WeakInterfacePtr.h"
#include "GameplayTagContainer.h"
#include "History/Seam_HubHistory.h"
#include "Analytics/Seam_AnalyticsSink.h"
#include "Events/WorldHub_EventTypes.h"
#include "WorldHub_EventLogSubsystem.generated.h"

class UWorldHub_StateHubSubsystem;
struct FWorldHub_Scope;

/**
 * Broadcast (server and clients) whenever a mutation event is appended to the log.
 * @param Event The flat (wire-safe) form of the appended event.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWorldHub_OnEventAppended, const FWorldHub_FlatEvent&, Event);

/**
 * EVENT SOURCING for the world hub.
 *
 * An append-only canonical log of hub mutations. It binds the hub's OnValueChanged and, AUTHORITY
 * ONLY (early-returns unless HasWorldAuthority before appending), records each change as an
 * FWorldHub_HubEvent. The lossless FInstancedStruct payload is LOCAL / SAVE only; the wire-safe value
 * is the FSeam_NetValue. A PII-safe summary is mirrored to ISeam_AnalyticsSink (held weakly, resolved
 * from the locator, optional) using only FSeam_AnalyticsAttr attributes.
 *
 * ReplayInto rebuilds a target hub by re-applying events through its authoritative SetValue path. The
 * subsystem implements ISeam_HubHistory (read + replay) so tooling never includes this header, and it
 * self-registers under DP.Service.WorldHub.EventLog.
 *
 * MaxEvents is a finite default cap (oldest trimmed); 0 is an explicit opt-in to an unbounded log.
 */
UCLASS()
class DESIGNPATTERNSWORLD_API UWorldHub_EventLogSubsystem
	: public UDP_WorldSubsystem
	, public ISeam_HubHistory
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Authority check (matches the state hub). True on server / standalone / listen-host. */
	bool HasWorldAuthority() const;

	// ---- Append / query -----------------------------------------------------------------------

	/** Append a fully-formed event. AUTHORITY ONLY: no-op on clients. Assigns the sequence/time fields. */
	void AppendEvent(const FWorldHub_HubEvent& Event);

	/**
	 * Copy events matching the filters into Out (Out is reset). An invalid KeyFilter matches all keys
	 * (with tag-parent matching when valid); a Global ScopeFilter with no faction/entity matches all
	 * scopes; SinceSequence returns only events strictly after it. Safe on clients.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Event")
	void QueryEvents(FGameplayTag KeyFilter, FWorldHub_Scope ScopeFilter, int64 SinceSequence, TArray<FWorldHub_HubEvent>& Out) const;

	/** Number of events currently retained. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|Event")
	int32 GetEventCount() const { return Events.Num(); }

	/**
	 * Re-apply every logged event with Sequence <= UpToSequence into TargetHub through its authoritative
	 * SetValue path (deterministic order). AUTHORITY ONLY. Pass <= 0 to replay the entire log.
	 * @return true if a non-null authority target was replayed into.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Event")
	bool ReplayInto(UWorldHub_StateHubSubsystem* TargetHub, int64 UpToSequence);

	// ---- ISeam_HubHistory ---------------------------------------------------------------------

	/** No checkpoint concept on the raw event log — delegates rewind to the history subsystem if present. */
	virtual bool RewindToCheckpoint(FGameplayTag CheckpointLabel) override;
	virtual int64 GetLatestEventSequence() const override;
	virtual void GetEventsSince(int64 Sequence, TArray<FInstancedStruct>& OutFlattened) const override;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/** Fired (server and clients) after an event is appended; carries the wire-safe flat form. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|WorldHub|Event")
	FWorldHub_OnEventAppended OnEventAppended;

	// ---- Tunables -----------------------------------------------------------------------------

	/** Cap on retained events (oldest trimmed past this). 0 = explicit opt-in to an unbounded log. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"), Category = "DesignPatterns|WorldHub|Event")
	int32 MaxEvents = 4096;

	/** When true, a PII-safe summary of each event is mirrored to the analytics sink (if connected). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|Event")
	bool bMirrorToAnalytics = false;

	/** Analytics event tag used for the mirrored aggregate event. Project-anchored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|Event")
	FGameplayTag AnalyticsEventTag;

private:
	/** The append-only event store. FInstancedStruct payloads are LOCAL/SAVE only. */
	UPROPERTY()
	TArray<FWorldHub_HubEvent> Events;

	/** Monotonic sequence counter (assigned on append, never reused). */
	int64 NextSequence = 1;

	/** The hub this observes (re-resolved lazily; never owned). */
	TWeakObjectPtr<UWorldHub_StateHubSubsystem> Hub;

	/** Optional analytics sink (held weakly, resolved from the locator). */
	TWeakInterfacePtr<ISeam_AnalyticsSink> AnalyticsSink;
	TWeakObjectPtr<UObject> CachedAnalyticsObject;

	/** True while ReplayInto is driving the target hub, so its echoed OnValueChanged is not re-logged. */
	bool bReplaying = false;

	/** Bound to the hub's OnValueChanged: builds and appends an event. AUTHORITY guarded at the TOP. */
	UFUNCTION()
	void OnHubValueChanged(FWorldHub_Scope Scope, FGameplayTag Key, FSeam_NetValue NewValue);

	/** Resolve / cache the world hub (engine-native world-subsystem lookup) and bind its delegate. */
	UWorldHub_StateHubSubsystem* ResolveHub();

	/** Resolve / cache the optional analytics sink from the locator. */
	ISeam_AnalyticsSink* ResolveAnalyticsSink();

	/** Mirror a PII-safe summary of Event to the analytics sink. */
	void MirrorEventToAnalytics(const FWorldHub_HubEvent& Event);

	/** Self-(un)register under DP.Service.WorldHub.EventLog. */
	void RegisterSelfAsService(bool bRegister);
};
