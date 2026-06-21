// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UNet_SpectatorComponent.generated.h"

/** Who a spectator is allowed to observe, enforced on the server. */
UENUM(BlueprintType)
enum class ENet_SpectatePolicy : uint8
{
	/** May spectate anyone (free-cam / observer / replay). */
	Anyone,
	/** May only spectate teammates (alive-team spectating in team modes). */
	TeammatesOnly,
	/** May only spectate enemies (kill-cam style). */
	EnemiesOnly
};

/** Fired (owning client) when the observed target changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNet_OnSpectatorTargetChanged, AActor*, NewTarget);

/**
 * Player-owned spectator / observer plumbing. Lives on a connection-owned actor (the player controller)
 * so its target-selection intent is a Server...WithValidation RPC. The server is authoritative over WHO a
 * client may observe: it validates each requested target against the spectate policy using the shared
 * ISeam_TeamAffinity seam (resolved from the locator), so a client cannot use spectator mode to wallhack
 * an enemy in a team mode that forbids it. The confirmed target is replicated back (OwnerOnly) and the
 * owning client points its view at it.
 *
 * This deliberately does NOT re-implement the engine's APlayerController spectator pawn/ViewTarget — it
 * drives target SELECTION + authorization and leaves the actual camera possession to the controller's
 * SetViewTargetWithBlend, which it calls on the owning client when the confirmed target replicates in.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSNET_API UNet_SpectatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNet_SpectatorComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Public API (owning client; routes to server) ----

	/** Request to observe Target. Server validates against SpectatePolicy before confirming. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Spectator")
	void RequestSpectate(AActor* Target);

	/** Request to cycle to the next valid target in PlayerStates order (server resolves the candidate). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Spectator")
	void RequestCycleTarget(bool bForward);

	/** Stop spectating (clears the target; the client returns to its own view). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Spectator")
	void RequestStopSpectating();

	/** The currently confirmed observed target (replicated to the owner). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Spectator")
	AActor* GetSpectateTarget() const { return SpectateTarget; }

	/** Who this spectator may observe (server-enforced). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Spectator")
	ENet_SpectatePolicy SpectatePolicy = ENet_SpectatePolicy::Anyone;

	/** Camera blend time (seconds) applied when the client switches view to the confirmed target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Spectator", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ViewBlendTime = 0.4f;

	/** Fired on the owning client when the confirmed target changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Spectator")
	FNet_OnSpectatorTargetChanged OnSpectatorTargetChanged;

protected:
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestSpectate(AActor* Target);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestCycleTarget(bool bForward);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestStopSpectating();

private:
	/** Server-side: true if this spectator may observe Candidate under SpectatePolicy. */
	bool IsTargetAllowed(const AActor* Candidate) const;

	/** Server-side: gather all currently-valid spectate candidates (pawns of other players). */
	void GatherCandidates(TArray<AActor*>& OutCandidates) const;

	/** OnRep: point the owning client's view at the confirmed target. */
	UFUNCTION()
	void OnRep_SpectateTarget();

	/** The server-confirmed observed target, replicated to the owner only (a live actor ref). */
	UPROPERTY(ReplicatedUsing = OnRep_SpectateTarget)
	TObjectPtr<AActor> SpectateTarget = nullptr;
};
