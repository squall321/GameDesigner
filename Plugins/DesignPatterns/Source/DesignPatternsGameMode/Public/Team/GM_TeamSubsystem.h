// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Identity/Seam_TeamAffinity.h"
#include "GameplayTagContainer.h"
#include "Team/GM_TeamTypes.h"
#include "GM_TeamSubsystem.generated.h"

class UGM_TeamComponent;
class UGM_TeamSettings;

/**
 * World-scoped team authority + the world's ISeam_TeamAffinity provider.
 *
 * RESPONSIBILITIES (authority-only writes; reads valid everywhere):
 *  - Assign / auto-balance actors across the configured team roster (server decides; the per-actor
 *    UGM_TeamComponent replicates the resulting tag so clients agree).
 *  - Answer GetTeamTag(actor) by reading the actor's UGM_TeamComponent.
 *  - Answer AreFriendly(a, b) under the configured EGM_TeamPolicy (free-for-all / team / ally-faction),
 *    honoring the designer-authored alliance table.
 *  - Publish friendly-fire policy/scalar so combat enforces it consistently.
 *  - Register itself in the service locator under GMTags::Service_TeamAffinity so combat/AI/HUD/respawn
 *    resolve a TScriptInterface<ISeam_TeamAffinity> without depending on the GameMode concrete type.
 *
 * It holds NO replicated state (HARD RULE 5: subsystems never replicate) — all networked team state
 * lives on the UGM_TeamComponent carriers. The subsystem only owns local policy + bookkeeping.
 */
UCLASS()
class DESIGNPATTERNSGAMEMODE_API UGM_TeamSubsystem : public UDP_WorldSubsystem, public ISeam_TeamAffinity
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISeam_TeamAffinity
	virtual FGameplayTag GetTeamTag_Implementation(const AActor* Actor) const override;
	virtual bool AreFriendly_Implementation(const AActor* A, const AActor* B) const override;
	//~ End ISeam_TeamAffinity

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/**
	 * Authority check (UWorldSubsystem has none of its own). True on standalone / listen-server / dedicated
	 * server worlds; false on a pure client.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * Authority-only: assign Actor to an explicit team tag (via its UGM_TeamComponent). No-op on clients
	 * or if the actor has no team component. Broadcasts GMTags::Bus_TeamChanged on a real change.
	 *
	 * @return True if the team actually changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Team")
	bool AssignTeam(AActor* Actor, FGameplayTag TeamTag);

	/**
	 * Authority-only: assign Actor to the least-populated team in the configured roster (auto-balance).
	 * No-op on clients, if the roster (AssignableTeams) is empty, or if the actor has no team component.
	 *
	 * @return The team the actor was assigned to (empty on failure / no-op).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Team")
	FGameplayTag AutoAssignBalancedTeam(AActor* Actor);

	/** Number of (team-component-bearing) actors currently on TeamTag in this world. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Team")
	int32 GetTeamPopulation(FGameplayTag TeamTag) const;

	/** The active friendly-fire policy (from settings, defensive fallback if the CDO is null). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Team")
	EGM_FriendlyFirePolicy GetFriendlyFirePolicy() const;

	/**
	 * Damage scalar combat should apply to friendly fire: 0 (Disabled), 1 (Enabled), or the configured
	 * reduced scalar. Combat multiplies friendly damage by this so the rule lives in one place.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Team")
	float GetFriendlyFireScalar() const;

	/** The active team policy (from settings, defensive fallback if the CDO is null). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Team")
	EGM_TeamPolicy GetTeamPolicy() const;

private:
	/** Resolve the team settings CDO, or null (callers apply a documented fallback). */
	const UGM_TeamSettings* GetSettings() const;

	/** True if the two team tags are allied per the explicit alliance table (symmetric). */
	bool AreTeamsExplicitlyAllied(const FGameplayTag& A, const FGameplayTag& B) const;

	/** Apply the configured policy to two resolved team tags. The core relation logic. */
	bool AreTeamsFriendly(const FGameplayTag& A, const FGameplayTag& B) const;

	/** Broadcast GMTags::Bus_TeamChanged for an actor whose team changed (authority path only). */
	void BroadcastTeamChanged(AActor* Actor, FGameplayTag PreviousTeam, FGameplayTag NewTeam) const;
};
