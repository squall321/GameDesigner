// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Net/Seam_NetValue.h"
#include "Rep_KillcamComponent.generated.h"

/**
 * A flat, net/save-friendly record of a death the killcam can replay.
 *
 * PURE POD (an FSeam_EntityId pair + an FSeam_NetValue + a time) — no UObject ref, no FInstancedStruct,
 * so it is safe to replicate as a plain UPROPERTY and to copy into a deferred capture. The killer/victim
 * are net/save-stable entity ids (not raw pointers) and the lethal magnitude is the closed-variant
 * FSeam_NetValue, so nothing unserializable can ride this struct across the wire.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_KillcamRecord
{
	GENERATED_BODY()

	/** Net/save-stable id of the entity that died (the victim). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Killcam")
	FSeam_EntityId Victim;

	/** Net/save-stable id of the entity credited with the kill, if known. Invalid => environmental. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Killcam")
	FSeam_EntityId Killer;

	/** Demo-relative time (seconds) of the lethal moment, if a recording is in progress; else 0. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Killcam")
	float DeathTimeSeconds = 0.f;

	/** Closed-variant lethal magnitude (e.g. the killing-blow damage). Net/save-safe. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Killcam")
	FSeam_NetValue LethalMagnitude;

	/** Optional kind tag describing the death (a game-authored cause tag). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Killcam")
	FGameplayTag CauseTag;

	/** True when this record names a victim (the minimum for a killcam to run). */
	bool IsValid() const { return Victim.IsValid(); }
};

class URep_ReplaySubsystem;

/** Fired locally on every machine when this component observes its owner's death (OnRep / authority). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRep_OnKillcamDeath, const FRep_KillcamRecord&, Record);

/**
 * URep_KillcamComponent — a PLAYER-OWNED carrier of the last death moment, so a death-cam can auto-replay
 * the last N seconds from the killer's point of view.
 *
 * This is the ONLY replicated state in the whole Replay module, and it lives where replicated state is
 * allowed (a UActorComponent). The server-authoritative death record is replicated to the owning client
 * via OnRep_LastDeath; the client's local killcam director reacts to OnKillcamDeath. Clients can also
 * report a candidate killcam moment up to the server via a player-owned Server RPC (with validation),
 * for cases where the client observed framing the server did not — but the AUTHORITATIVE record is always
 * the server's.
 *
 * AUTHORITY: every mutator of the replicated record (ReportDeath, the Server RPC) guards HasAuthority()
 * at the TOP. The Server RPC is WithValidation. Replication is set up via SetIsReplicatedByDefault in the
 * constructor + GetLifetimeReplicatedProps + DOREPLIFETIME + OnRep_LastDeath.
 *
 * The component does NOT tick and holds no cross-world refs.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Sockets, Tags, ComponentTick, Cooking, Collision))
class DESIGNPATTERNSREPLAY_API URep_KillcamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URep_KillcamComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Server-authoritative: record the owner's death. Guards HasAuthority() at the top; sets the
	 * replicated LastDeath (which replicates to the owning client and fires OnRep there) and also fires
	 * the local delegate on the server. Safe to call from server-side death handling (game mode / damage).
	 *
	 * @param Record  The death record (victim/killer ids, time, magnitude, cause).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Killcam")
	void ReportDeath(const FRep_KillcamRecord& Record);

	/**
	 * Client->server intent: report a killcam moment the client observed (e.g. precise local framing).
	 * Validated and authority-guarded server-side; the server decides whether to adopt it as LastDeath.
	 * Routed through THIS player-owned component (the canonical client->server path).
	 */
	UFUNCTION(Server, Reliable, WithValidation, Category = "DesignPatterns|Replay|Killcam")
	void Server_ReportKillcamMoment(const FRep_KillcamRecord& Record);

	/** The last replicated death record (valid only after a death). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Killcam")
	const FRep_KillcamRecord& GetLastDeath() const { return LastDeath; }

	/** Fired locally when a death record arrives (server set, or client OnRep). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Killcam")
	FRep_OnKillcamDeath OnKillcamDeath;

private:
	/**
	 * The replicated last-death record. Replicated to the owning connection only (it is per-player
	 * cosmetic). Flat POD (entity ids + FSeam_NetValue) — never a raw FInstancedStruct on the wire.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_LastDeath)
	FRep_KillcamRecord LastDeath;

	/** Client-side OnRep: fires OnKillcamDeath so the local killcam director can begin the death-cam. */
	UFUNCTION()
	void OnRep_LastDeath();

	/** Broadcast the death locally (and onto the bus) — shared by the server set path and OnRep. */
	void NotifyDeathLocally();
};
