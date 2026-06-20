// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Analytics_PlayerIdProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UAnalytics_PlayerIdProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam through which the Analytics module obtains a STABLE player identity for experiment
 * bucketing — without ever inventing one itself.
 *
 * Why a seam (and why the module must never mint identity):
 *  - Player identity is a project/platform concern (a logged-in account id, a platform user
 *    id, a persisted install GUID, an explicitly-consented analytics id...). The Analytics
 *    module has no business deciding what "the player" is, and minting its own id would be a
 *    privacy footgun. So it RESOLVES this seam from the service locator (by the tag in
 *    Analytics_DeveloperSettings::PlayerIdProviderServiceTag) and uses whatever the host
 *    supplies.
 *
 *  - The returned string is treated as opaque and SENSITIVE. The Analytics module never puts
 *    it directly into an event attribute (attributes are FSeam_NetValue, which structurally
 *    cannot carry a raw id). Instead it HASHES the id into a stable, non-reversible bucket
 *    key used only for A/B experiment assignment. The raw id never leaves this module.
 *
 * Inert default: when this seam is unresolved, the subsystem behaves as an unidentified
 * session — experiment bucketing falls back to a per-session random bucket and no stable id
 * is recorded. Telemetry still works; only deterministic cross-session bucketing is absent.
 *
 * Game-thread only.
 */
class DESIGNPATTERNSANALYTICS_API IAnalytics_PlayerIdProvider
{
	GENERATED_BODY()

public:
	/**
	 * Return a stable, opaque identifier for the current player, or an empty string if no
	 * identity is currently available (e.g. not yet signed in). The Analytics module hashes
	 * this for experiment bucketing and NEVER records it verbatim.
	 *
	 * Implementations should return the SAME string across sessions for the same player so
	 * experiment assignment is deterministic; returning empty is always safe.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Analytics")
	FString GetStablePlayerId() const;
};
