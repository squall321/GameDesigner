// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Interact_InteractorComponent.h"

#include "Interfaces/Interact_Interactable.h"
#include "Focus/Interact_FocusStrategy.h"
#include "Data/Interact_VerbDefinition.h"
#include "DesignPatternsInteractionModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"
#include "Net/UnrealNetwork.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UInteract_InteractorComponent::UInteract_InteractorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Player-owned interactor: replicate so the owner sees its own active interaction (hold bars).
	SetIsReplicatedByDefault(true);

	// Default focus policy: line-of-sight targeting (most game-typical). Designers can swap inline.
	FocusStrategy = CreateDefaultSubobject<UInteract_FocusStrategy_LineOfSight>(TEXT("FocusStrategy"));
}

void UInteract_InteractorComponent::BeginPlay()
{
	Super::BeginPlay();

	// Detection ticking is only meaningful where a local view exists (the owning client / standalone).
	// Dedicated-server-only owners still tick cheaply (no candidates) and the server path is RPC-driven.
}

void UInteract_InteractorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// If we are mid-interaction on the server when torn down, end it cleanly so the interactable
	// is not left in a started state.
	if (HasAuthority() && ActiveVerb.IsValid())
	{
		ServerEndInteractAuthoritative(EInteract_EndReason::Interrupted);
	}
	Super::EndPlay(EndPlayReason);
}

void UInteract_InteractorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Only the owning client needs to observe its own active interaction (for hold progress UI).
	DOREPLIFETIME_CONDITION(UInteract_InteractorComponent, ActiveVerb, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UInteract_InteractorComponent, ActiveStartServerTimeSeconds, COND_OwnerOnly);
}

void UInteract_InteractorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Local focus detection runs only where there is a local view to query (the controlling client
	// or standalone). On a dedicated server / remote proxies there is no meaningful local reticle.
	const AActor* Owner = GetOwner();
	const APawn* OwnerPawn = Cast<APawn>(Owner);
	const bool bHasLocalView = OwnerPawn && OwnerPawn->IsLocallyControlled();
	if (!bHasLocalView)
	{
		return;
	}

	// Throttle focus updates to FocusUpdateHz to bound trace cost.
	FocusAccumulator += DeltaTime;
	const float Interval = 1.f / FMath::Max(1.f, FocusUpdateHz);
	if (FocusAccumulator < Interval)
	{
		return;
	}
	FocusAccumulator = 0.f;

	UpdateLocalFocus();
}

//~ Local focus -------------------------------------------------------------------------------

void UInteract_InteractorComponent::UpdateLocalFocus()
{
	FInteract_Query Query = BuildQueryFromOwnerView(FGameplayTag());

	TArray<FInteract_Candidate> Candidates;
	DetectCandidates(Query, Candidates);

	int32 BestIndex = INDEX_NONE;
	if (FocusStrategy && Candidates.Num() > 0)
	{
		BestIndex = FocusStrategy->SelectBestCandidate(Candidates, Query);
	}

	AActor* NewFocusActor = nullptr;
	UObject* NewInteractable = nullptr;
	FInteract_PromptInfo NewPrompt;

	if (Candidates.IsValidIndex(BestIndex) && Candidates[BestIndex].IsValidCandidate())
	{
		const FInteract_Candidate& Chosen = Candidates[BestIndex];
		NewFocusActor = Chosen.Actor.Get();
		NewInteractable = Chosen.InteractableObject.Get();

		if (NewInteractable && NewInteractable->Implements<UInteract_Interactable>())
		{
			NewPrompt = IInteract_Interactable::Execute_GetInteractionPrompt(NewInteractable, Query);
		}
	}

	// Detect a focus change (by interactable object identity) and notify locally.
	const UObject* PrevInteractable = FocusInteractableObject.Get();
	if (PrevInteractable != NewInteractable)
	{
		FocusActor = NewFocusActor;
		FocusInteractableObject = NewInteractable;
		FocusPrompt = NewPrompt;

		OnFocusChanged.Broadcast(NewFocusActor, FocusPrompt);
	}
	else
	{
		// Same focus target — keep the prompt fresh (enabled state / text may change over time).
		FocusPrompt = NewPrompt;
	}
}

//~ Local request API -------------------------------------------------------------------------

void UInteract_InteractorComponent::RequestInteract(FGameplayTag DesiredVerb)
{
	// Standalone/listen-host authority can act directly; remote clients route through the server.
	if (HasAuthority())
	{
		const EInteract_Result Result = ServerBeginInteractAuthoritative(DesiredVerb);
		ClientInteractResult(Result, DesiredVerb);
		return;
	}

	ServerInteract(DesiredVerb);
}

void UInteract_InteractorComponent::RequestEndInteract(EInteract_EndReason Reason)
{
	if (HasAuthority())
	{
		ServerEndInteractAuthoritative(Reason);
		return;
	}

	ServerEndInteract(Reason);
}

//~ Server RPCs -------------------------------------------------------------------------------

bool UInteract_InteractorComponent::ServerInteract_Validate(FGameplayTag DesiredVerb)
{
	// There is intentionally NO client-named target to validate here (the server re-derives it), so
	// the RPC payload is always structurally acceptable: the verb is either empty ("default verb")
	// or a tag the server will itself check against the target's supported verbs. Returning true
	// keeps the connection open; any abuse is caught by the server-side re-derivation in _Implementation.
	return true;
}

void UInteract_InteractorComponent::ServerInteract_Implementation(FGameplayTag DesiredVerb)
{
	// Re-derive + re-validate the target server-side; never trust the client's idea of "what".
	const EInteract_Result Result = ServerBeginInteractAuthoritative(DesiredVerb);
	ClientInteractResult(Result, DesiredVerb);
}

bool UInteract_InteractorComponent::ServerEndInteract_Validate(EInteract_EndReason Reason)
{
	return true;
}

void UInteract_InteractorComponent::ServerEndInteract_Implementation(EInteract_EndReason Reason)
{
	ServerEndInteractAuthoritative(Reason);
}

void UInteract_InteractorComponent::ClientInteractResult_Implementation(EInteract_Result Result, FGameplayTag Verb)
{
	UE_LOG(LogDP, Verbose, TEXT("[Interact] ClientInteractResult: %s verb=%s"),
		*UEnum::GetValueAsString(Result), *Verb.ToString());

	// The started/completed delegates are driven by replication (OnRep) so they fire consistently
	// for both predicted and server-only flows. This result is purely UI feedback (e.g. play a
	// "denied" sound on a non-Success result).
}

//~ Server-side authoritative core ------------------------------------------------------------

EInteract_Result UInteract_InteractorComponent::ServerBeginInteractAuthoritative(FGameplayTag DesiredVerb)
{
	if (!HasAuthority())
	{
		return EInteract_Result::NotAllowed;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return EInteract_Result::NotAllowed;
	}

	// If already interacting, end the previous one first (treat a new request as a switch).
	if (ActiveVerb.IsValid())
	{
		ServerEndInteractAuthoritative(EInteract_EndReason::Interrupted);
	}

	// Re-derive the target authoritatively from the server's own view of the pawn.
	FInteract_Query Query = BuildQueryFromOwnerView(DesiredVerb);

	TArray<FInteract_Candidate> Candidates;
	DetectCandidates(Query, Candidates);

	int32 BestIndex = INDEX_NONE;
	if (FocusStrategy && Candidates.Num() > 0)
	{
		BestIndex = FocusStrategy->SelectBestCandidate(Candidates, Query);
	}

	if (!Candidates.IsValidIndex(BestIndex) || !Candidates[BestIndex].IsValidCandidate())
	{
		return EInteract_Result::NoTarget;
	}

	const FInteract_Candidate& Chosen = Candidates[BestIndex];
	UObject* Interactable = Chosen.InteractableObject.Get();
	AActor* TargetActor = Chosen.Actor.Get();
	if (!Interactable || !TargetActor || !Interactable->Implements<UInteract_Interactable>())
	{
		return EInteract_Result::NoTarget;
	}

	// Re-check eligibility on the server (CanInteract is read-only and runs everywhere).
	if (!IInteract_Interactable::Execute_CanInteract(Interactable, Query))
	{
		return EInteract_Result::Rejected;
	}

	// Resolve the effective verb (DesiredVerb if supported, else the interactable's default).
	FGameplayTag EffectiveVerb;
	if (!ResolveEffectiveVerb(Interactable, Query, EffectiveVerb))
	{
		return EInteract_Result::UnsupportedVerb;
	}

	// Build the authoritative context and call the interactable's authority-only BeginInteract.
	FInteract_Context Context;
	Context.Instigator = Owner;
	Context.Verb = EffectiveVerb;
	Context.StartServerTimeSeconds = GetServerTimeSeconds();

	const bool bBegan = IInteract_Interactable::Execute_BeginInteract(Interactable, Context);
	if (!bBegan)
	{
		return EInteract_Result::Rejected;
	}

	// Record + replicate the active interaction, then broadcast the Begin bus event.
	SetActiveInteraction_Server(EffectiveVerb, Context.StartServerTimeSeconds, Interactable, TargetActor);
	BroadcastBusEvent(InteractNativeTags::Bus_Interact_Begin, TargetActor, EffectiveVerb, EInteract_EndReason::Completed);

	UE_LOG(LogDP, Verbose, TEXT("[Interact] %s began '%s' on %s"),
		*Owner->GetName(), *EffectiveVerb.ToString(), *TargetActor->GetName());

	return EInteract_Result::Success;
}

void UInteract_InteractorComponent::ServerEndInteractAuthoritative(EInteract_EndReason Reason)
{
	if (!HasAuthority())
	{
		return;
	}
	if (!ActiveVerb.IsValid())
	{
		return;
	}

	UObject* Interactable = ActiveInteractableObject.Get();
	AActor* TargetActor = ActiveTargetActor.Get();
	const FGameplayTag EndedVerb = ActiveVerb;

	if (Interactable && Interactable->Implements<UInteract_Interactable>())
	{
		FInteract_Context Context;
		Context.Instigator = GetOwner();
		Context.Verb = EndedVerb;
		Context.StartServerTimeSeconds = ActiveStartServerTimeSeconds;
		IInteract_Interactable::Execute_EndInteract(Interactable, Context, Reason);
	}

	// Broadcast Complete vs Cancel based on the reason.
	const FGameplayTag Channel = (Reason == EInteract_EndReason::Completed)
		? InteractNativeTags::Bus_Interact_Complete
		: InteractNativeTags::Bus_Interact_Cancel;
	BroadcastBusEvent(Channel, TargetActor, EndedVerb, Reason);

	// Surface the completion locally on the server immediately (clients get it via OnRep clear).
	OnInteractCompleted.Broadcast(TargetActor, EndedVerb, Reason);

	ClearActiveInteraction_Server();

	UE_LOG(LogDP, Verbose, TEXT("[Interact] ended '%s' reason=%s"),
		*EndedVerb.ToString(), *UEnum::GetValueAsString(Reason));
}

//~ Replicated state mutators (authority-guarded) ---------------------------------------------

void UInteract_InteractorComponent::SetActiveInteraction_Server(FGameplayTag Verb, double StartTime, UObject* Interactable, AActor* TargetActor)
{
	if (!HasAuthority())
	{
		return;
	}

	ActiveVerb = Verb;
	ActiveStartServerTimeSeconds = StartTime;
	ActiveInteractableObject = Interactable;
	ActiveTargetActor = TargetActor;

	// On a listen host / standalone the OnRep does not fire for the authority, so notify here too.
	OnInteractStarted.Broadcast(TargetActor, Verb);
}

void UInteract_InteractorComponent::ClearActiveInteraction_Server()
{
	if (!HasAuthority())
	{
		return;
	}

	ActiveVerb = FGameplayTag();
	ActiveStartServerTimeSeconds = 0.0;
	ActiveInteractableObject = nullptr;
	ActiveTargetActor = nullptr;
}

void UInteract_InteractorComponent::OnRep_ActiveVerb()
{
	// Transition detection on the owning client, using our own shadow of the previous value so the
	// logic does not depend on the engine's typed "previous value" OnRep parameter.
	const FGameplayTag PreviousVerb = LastObservedActiveVerb;
	LastObservedActiveVerb = ActiveVerb;

	if (!PreviousVerb.IsValid() && ActiveVerb.IsValid())
	{
		// Started: we do not have the server-side target pointer on the client, so report the
		// current local focus actor (the player was looking at it) as the best-effort target.
		OnInteractStarted.Broadcast(FocusActor.Get(), ActiveVerb);
	}
	else if (PreviousVerb.IsValid() && !ActiveVerb.IsValid())
	{
		// Ended: the reason is not replicated through this pair; report Completed as the default.
		// (Authoritative reason is delivered to listeners via the DP.Bus.Interact.* events.)
		OnInteractCompleted.Broadcast(FocusActor.Get(), PreviousVerb, EInteract_EndReason::Completed);
	}
}

//~ Query / detection -------------------------------------------------------------------------

FInteract_Query UInteract_InteractorComponent::BuildQueryFromOwnerView(FGameplayTag DesiredVerb) const
{
	FInteract_Query Query;
	Query.DesiredVerb = DesiredVerb;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return Query;
	}
	Query.Instigator = Owner;

	// Prefer the controller's view point (camera) when available; this is consistent server-side
	// because the controller's control rotation replicates to the server.
	FVector ViewLoc = Owner->GetActorLocation();
	FRotator ViewRot = Owner->GetActorRotation();

	if (const APawn* OwnerPawn = Cast<APawn>(Owner))
	{
		if (AController* Controller = OwnerPawn->GetController())
		{
			Controller->GetPlayerViewPoint(ViewLoc, ViewRot);
		}
		else
		{
			// No controller (server proxy without possession): fall back to the pawn's view point.
			OwnerPawn->GetActorEyesViewPoint(ViewLoc, ViewRot);
		}
	}

	Query.ViewLocation = ViewLoc;
	Query.ViewDirection = ViewRot.Vector().GetSafeNormal();
	return Query;
}

void UInteract_InteractorComponent::DetectCandidates(const FInteract_Query& Query, TArray<FInteract_Candidate>& OutCandidates) const
{
	OutCandidates.Reset();

	const UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	const float Range = FMath::Max(0.f, Detection.Range);
	if (Range <= 0.f)
	{
		return;
	}

	// Overlap a sphere of Range centered on the view location, on the configured channel.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(Interact_Detect), /*bTraceComplex=*/false, Owner);
	Params.AddIgnoredActor(Owner);

	TArray<FOverlapResult> Overlaps;
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Range);
	World->OverlapMultiByChannel(
		Overlaps,
		Query.ViewLocation,
		FQuat::Identity,
		Detection.Channel.GetValue(),
		Sphere,
		Params);

	const float CosCone = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Detection.ConeHalfAngleDeg, 0.f, 180.f)));

	// De-dupe interactables across multiple overlapping components of the same actor.
	TSet<UObject*> SeenInteractables;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (OutCandidates.Num() >= FMath::Max(1, MaxCandidates))
		{
			break;
		}

		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == Owner)
		{
			continue;
		}

		UObject* Interactable = FindInteractableOn(HitActor);
		if (!Interactable || !Interactable->Implements<UInteract_Interactable>())
		{
			continue;
		}
		if (SeenInteractables.Contains(Interactable))
		{
			continue;
		}

		// Read-only eligibility gate.
		if (!IInteract_Interactable::Execute_CanInteract(Interactable, Query))
		{
			continue;
		}

		// Geometry against the actor's focus point (its origin; interactables can refine via prompt).
		const FVector FocusLoc = HitActor->GetActorLocation();
		const FVector ToTarget = FocusLoc - Query.ViewLocation;
		const float Dist = ToTarget.Size();
		if (Dist > Range)
		{
			continue;
		}

		const FVector Dir = (Dist > KINDA_SMALL_NUMBER) ? (ToTarget / Dist) : Query.ViewDirection;
		const float Dot = FVector::DotProduct(Query.ViewDirection, Dir);

		// Cone gate.
		if (Dot < CosCone)
		{
			continue;
		}

		const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));

		// Line-of-sight gate (optional). A blocking hit before the target fails the check.
		bool bHasLOS = true;
		if (Detection.bRequireLineOfSight)
		{
			FHitResult LosHit;
			FCollisionQueryParams LosParams(SCENE_QUERY_STAT(Interact_LOS), /*bTraceComplex=*/false, Owner);
			LosParams.AddIgnoredActor(Owner);
			LosParams.AddIgnoredActor(HitActor); // we want to know if anything ELSE blocks us.
			const bool bBlocked = World->LineTraceTestByChannel(
				Query.ViewLocation, FocusLoc, Detection.Channel.GetValue(), LosParams);
			bHasLOS = !bBlocked;

			if (!bHasLOS)
			{
				// Hard requirement: drop occluded candidates entirely.
				continue;
			}
		}

		FInteract_Candidate Candidate;
		Candidate.Actor = HitActor;
		Candidate.InteractableObject = Interactable;
		Candidate.FocusLocation = FocusLoc;
		Candidate.Distance = Dist;
		Candidate.AngleDeg = AngleDeg;
		Candidate.bHasLineOfSight = bHasLOS;

		// Pull selection priority from the interactable's supported-verb default if it exposes one
		// via the prompt (bEnabled false still allows focus but a disabled prompt). Priority defaults
		// to 0 unless the interactable encodes it; kept here as the actor's net priority hint.
		Candidate.Priority = 0;

		SeenInteractables.Add(Interactable);
		OutCandidates.Add(Candidate);
	}
}

UObject* UInteract_InteractorComponent::FindInteractableOn(AActor* Actor) const
{
	if (!Actor)
	{
		return nullptr;
	}

	// The actor itself may implement the interface.
	if (Actor->Implements<UInteract_Interactable>())
	{
		return Actor;
	}

	// Otherwise look for the first component implementing it.
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component && Component->Implements<UInteract_Interactable>())
		{
			return Component;
		}
	}
	return nullptr;
}

bool UInteract_InteractorComponent::ResolveEffectiveVerb(UObject* Interactable, const FInteract_Query& Query, FGameplayTag& OutVerb) const
{
	OutVerb = FGameplayTag();
	if (!Interactable || !Interactable->Implements<UInteract_Interactable>())
	{
		return false;
	}

	FGameplayTagContainer Supported;
	IInteract_Interactable::Execute_GetSupportedVerbs(Interactable, Supported);
	if (Supported.IsEmpty())
	{
		return false;
	}

	// If the client asked for a specific verb and it is supported, use it.
	if (Query.DesiredVerb.IsValid() && Supported.HasTagExact(Query.DesiredVerb))
	{
		OutVerb = Query.DesiredVerb;
		return true;
	}

	// Otherwise use the interactable's default (first supported) verb.
	for (const FGameplayTag& Tag : Supported)
	{
		if (Tag.IsValid())
		{
			OutVerb = Tag;
			return true;
		}
	}
	return false;
}

//~ Hold progress -----------------------------------------------------------------------------

float UInteract_InteractorComponent::GetActiveHoldProgress() const
{
	if (!ActiveVerb.IsValid())
	{
		return 0.f;
	}

	// Resolve the verb definition to learn whether this is a hold verb and its duration.
	float HoldSeconds = 0.f;
	bool bHold = false;
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		if (const UInteract_VerbDefinition* Def = Cast<UInteract_VerbDefinition>(Registry->FindByTag(ActiveVerb)))
		{
			bHold = Def->bHoldToActivate;
			HoldSeconds = Def->HoldSeconds;
		}
	}

	if (!bHold || HoldSeconds <= 0.f)
	{
		return 1.f; // Instant verbs are "fully held" the moment they are active.
	}

	const double Now = GetServerTimeSeconds();
	const double Elapsed = Now - ActiveStartServerTimeSeconds;
	return FMath::Clamp(static_cast<float>(Elapsed / HoldSeconds), 0.f, 1.f);
}

//~ Bus + misc helpers ------------------------------------------------------------------------

void UInteract_InteractorComponent::BroadcastBusEvent(FGameplayTag Channel, AActor* Target, FGameplayTag Verb, EInteract_EndReason Reason) const
{
	if (!Channel.IsValid())
	{
		return;
	}

	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FInteract_BusPayload Payload;
	Payload.Instigator = GetOwner();
	Payload.Target = Target;
	Payload.Verb = Verb;
	Payload.EndReason = Reason;
	Payload.ServerTimeSeconds = GetServerTimeSeconds();

	FInstancedStruct Wrapped = FInstancedStruct::Make(Payload);
	Bus->BroadcastPayload(Channel, Wrapped, GetOwner());
}

bool UInteract_InteractorComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

double UInteract_InteractorComponent::GetServerTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	// TimeSeconds is the world clock; on the authority it is the canonical "server time" used for
	// hold/timeout math. Clients read it from their own world clock, which is close enough for UI.
	return World->GetTimeSeconds();
}
