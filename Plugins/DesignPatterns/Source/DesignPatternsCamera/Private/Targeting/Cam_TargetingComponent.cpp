// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Targeting/Cam_TargetingComponent.h"
#include "Targeting/Cam_TargetSelectionStrategy.h"
#include "Settings/Cam_DeveloperSettings.h"
#include "Cam_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"

#include "Identity/Seam_EntityIdentity.h"
#include "Input/Seam_InputModeArbiter.h"
#include "FSM/DPBlackboard.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"

// FInstancedStruct for the bus payload, version-gated per the engine band.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UCam_TargetingComponent::UCam_TargetingComponent()
{
	// Targeting must re-evaluate continuously while soft-acquiring / hard-locked.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// The component itself replicates ONLY so its Server... RPC can route from the owning client.
	// No cosmetic state is a replicated property — framing stays local.
	SetIsReplicatedByDefault(true);
}

void UCam_TargetingComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterAsTargetSourceService();
}

void UCam_TargetingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PopLockOnInputMode();
	UnregisterTargetSourceService();
	Super::EndPlay(EndPlayReason);
}

//------------------------------------------------------------------------------------------------
// ICam_TargetSource
//------------------------------------------------------------------------------------------------

FSeam_EntityId UCam_TargetingComponent::GetCurrentTarget_Implementation() const
{
	return CurrentTargetId;
}

bool UCam_TargetingComponent::HasTarget_Implementation() const
{
	return CurrentTargetId.IsValid();
}

//------------------------------------------------------------------------------------------------
// Tick / acquisition loop
//------------------------------------------------------------------------------------------------

void UCam_TargetingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Only the locally-controlled player drives targeting/framing. On a dedicated server with no
	// local controller this is a no-op (the server only reacts to ServerSetSoftLockTarget RPCs).
	const APlayerController* PC = ResolveOwningPlayerController();
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	TimeSinceLastGather += DeltaTime;

	// Hard lock re-validates the locked target every tick; soft-acquire is throttled.
	const bool bShouldGather = bHardLocked
		|| (bSoftAcquireWhenUnlocked && TimeSinceLastGather >= FMath::Max(0.f, SoftAcquireInterval));
	if (!bShouldGather)
	{
		return;
	}
	TimeSinceLastGather = 0.f;

	FCam_TargetingView View;
	if (!BuildView(View))
	{
		return;
	}

	TArray<FCam_TargetCandidate> Candidates;
	GatherCandidates(View, Candidates);
	RebuildCandidateRing(Candidates, View);

	if (bHardLocked)
	{
		// Keep the locked id if it is still a valid candidate; otherwise re-acquire the best.
		const bool bStillValid = Candidates.ContainsByPredicate(
			[this](const FCam_TargetCandidate& C) { return C.EntityId == CurrentTargetId; });
		if (!bStillValid)
		{
			const FSeam_EntityId Best = SelectBest(Candidates, View);
			if (Best.IsValid())
			{
				SetCurrentTarget(Best, /*bHardLock*/ true);
			}
			else
			{
				// Lost every candidate: drop the hard lock.
				ReleaseLock();
			}
		}
	}
	else if (bSoftAcquireWhenUnlocked)
	{
		const FSeam_EntityId Best = SelectBest(Candidates, View);
		if (Best != CurrentTargetId)
		{
			SetCurrentTarget(Best, /*bHardLock*/ false);
		}
	}
}

//------------------------------------------------------------------------------------------------
// Lock-on control
//------------------------------------------------------------------------------------------------

FSeam_EntityId UCam_TargetingComponent::ToggleLock()
{
	if (bHardLocked)
	{
		ReleaseLock();
		return FSeam_EntityId::Invalid();
	}
	return AcquireLock();
}

FSeam_EntityId UCam_TargetingComponent::AcquireLock()
{
	FCam_TargetingView View;
	if (!BuildView(View))
	{
		return FSeam_EntityId::Invalid();
	}

	TArray<FCam_TargetCandidate> Candidates;
	GatherCandidates(View, Candidates);
	RebuildCandidateRing(Candidates, View);

	const FSeam_EntityId Best = SelectBest(Candidates, View);
	if (!Best.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("[Cam_Targeting] %s: AcquireLock found no candidate."), *GetNameSafe(GetOwner()));
		return FSeam_EntityId::Invalid();
	}

	bHardLocked = true;
	PushLockOnInputMode();
	SetCurrentTarget(Best, /*bHardLock*/ true);
	return Best;
}

void UCam_TargetingComponent::ReleaseLock()
{
	if (!bHardLocked)
	{
		return;
	}
	bHardLocked = false;
	PopLockOnInputMode();

	// On release, fall back to soft-acquire (if enabled) on the next tick; clear the lock now.
	SetCurrentTarget(bSoftAcquireWhenUnlocked ? CurrentTargetId : FSeam_EntityId::Invalid(), /*bHardLock*/ false);
}

FSeam_EntityId UCam_TargetingComponent::CycleTarget(int32 Direction)
{
	if (!bHardLocked || CandidateRing.Num() < 2 || Direction == 0)
	{
		return CurrentTargetId;
	}

	const int32 CurrentIndex = CandidateRing.IndexOfByKey(CurrentTargetId);
	const int32 Step = Direction > 0 ? 1 : -1;
	const int32 Count = CandidateRing.Num();
	// If the current id isn't in the ring (lost it), start from the ends.
	const int32 StartIndex = (CurrentIndex == INDEX_NONE) ? (Step > 0 ? -1 : 0) : CurrentIndex;
	const int32 NextIndex = ((StartIndex + Step) % Count + Count) % Count;

	const FSeam_EntityId NextId = CandidateRing[NextIndex];
	SetCurrentTarget(NextId, /*bHardLock*/ true);
	return NextId;
}

AActor* UCam_TargetingComponent::GetCurrentTargetActor() const
{
	if (!CurrentTargetId.IsValid())
	{
		return nullptr;
	}
	// Re-derive the actor this frame by scanning candidates; we never cache a raw actor across frames.
	FCam_TargetingView View;
	if (!BuildView(View))
	{
		return nullptr;
	}
	TArray<FCam_TargetCandidate> Candidates;
	GatherCandidates(View, Candidates);
	for (const FCam_TargetCandidate& C : Candidates)
	{
		if (C.EntityId == CurrentTargetId)
		{
			return C.Actor.Get();
		}
	}
	return nullptr;
}

//------------------------------------------------------------------------------------------------
// Internals: tunables / view
//------------------------------------------------------------------------------------------------

void UCam_TargetingComponent::ResolveTunables(float& OutRange, float& OutHalfAngleDeg, float& OutOverlapRadius) const
{
	const UCam_DeveloperSettings* Settings = UCam_DeveloperSettings::Get();

	// Defensive fallbacks if the settings CDO is somehow null (documented per the hard rules).
	const float FallbackRange = Settings ? Settings->DefaultMaxTargetRange : 2000.f;
	const float FallbackHalf = Settings ? Settings->DefaultAcquisitionHalfAngleDeg : 60.f;
	const float FallbackRadius = Settings ? Settings->DefaultCandidateOverlapRadius : 2000.f;

	OutRange = MaxTargetRange > 0.f ? MaxTargetRange : FallbackRange;
	OutHalfAngleDeg = AcquisitionHalfAngleDeg > 0.f ? AcquisitionHalfAngleDeg : FallbackHalf;
	OutOverlapRadius = CandidateOverlapRadius > 0.f ? CandidateOverlapRadius : FallbackRadius;
}

bool UCam_TargetingComponent::BuildView(FCam_TargetingView& OutView) const
{
	APlayerController* PC = ResolveOwningPlayerController();
	if (!PC)
	{
		return false;
	}

	// Prefer the camera manager's POV (true view), fall back to the controller's control rotation.
	FVector ViewLoc = FVector::ZeroVector;
	FRotator ViewRot = FRotator::ZeroRotator;
	if (const APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
	{
		ViewLoc = CamMgr->GetCameraLocation();
		ViewRot = CamMgr->GetCameraRotation();
	}
	else
	{
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	}

	float Range, HalfAngle, Radius;
	ResolveTunables(Range, HalfAngle, Radius);

	OutView.ViewLocation = ViewLoc;
	OutView.ViewForward = ViewRot.Vector();
	OutView.MaxRange = Range;
	OutView.HalfAngleDeg = HalfAngle;
	OutView.CurrentTargetId = CurrentTargetId;

	// Bind the shared blackboard provider if the owning actor exposes one (for threat strategies).
	if (AActor* OwnerActor = GetOwner())
	{
		if (OwnerActor->Implements<UDP_BlackboardProvider>())
		{
			OutView.Blackboard.SetObject(OwnerActor);
			OutView.Blackboard.SetInterface(Cast<IDP_BlackboardProvider>(OwnerActor));
		}
	}
	return true;
}

//------------------------------------------------------------------------------------------------
// Internals: gather / select
//------------------------------------------------------------------------------------------------

int32 UCam_TargetingComponent::GatherCandidates(const FCam_TargetingView& View, TArray<FCam_TargetCandidate>& OutCandidates) const
{
	OutCandidates.Reset();

	const UWorld* World = GetWorld();
	const AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return 0;
	}

	float Range, HalfAngle, Radius;
	ResolveTunables(Range, HalfAngle, Radius);

	// Sphere overlap around the OWNER (not the camera) so candidates near the player are gathered;
	// cone test below is relative to the VIEW. Use object-type query so we collect pawns/dynamics.
	FCollisionObjectQueryParams ObjectParams;
	if (CandidateObjectTypes.Num() > 0)
	{
		for (const TEnumAsByte<EObjectTypeQuery>& OT : CandidateObjectTypes)
		{
			ObjectParams.AddObjectTypesToQuery(UEngineTypes::ConvertToCollisionChannel(OT));
		}
	}
	else
	{
		ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
		ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(Cam_GatherCandidates), /*bTraceComplex*/ false, OwnerActor);
	QueryParams.AddIgnoredActor(OwnerActor);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		OwnerActor->GetActorLocation(),
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(Radius),
		QueryParams);

	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(HalfAngle));
	const float RangeSq = Range * Range;

	TSet<const AActor*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!Actor || Actor == OwnerActor || Seen.Contains(Actor))
		{
			continue;
		}
		Seen.Add(Actor);

		// Identity: only actors with a stable id can be targeted by id (the whole point of the seam).
		FSeam_EntityId Id;
		FGameplayTag Archetype;
		if (!ReadIdentity(Actor, Id, Archetype))
		{
			continue;
		}

		// Optional archetype filter (hierarchy-aware).
		if (RequiredArchetypeTag.IsValid() && !Archetype.MatchesTag(RequiredArchetypeTag))
		{
			continue;
		}

		const FVector Focus = Actor->GetActorLocation();
		const FVector ToTarget = Focus - View.ViewLocation;
		const float DistSq = ToTarget.SizeSquared();
		if (DistSq > RangeSq || DistSq <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const float Dist = FMath::Sqrt(DistSq);
		const FVector Dir = ToTarget / Dist;
		const float Dot = FVector::DotProduct(View.ViewForward, Dir);
		if (Dot < CosHalfAngle)
		{
			continue; // outside the acquisition cone
		}

		// Optional line-of-sight gate.
		if (bRequireLineOfSight)
		{
			FHitResult Hit;
			FCollisionQueryParams LosParams(SCENE_QUERY_STAT(Cam_LineOfSight), /*bTraceComplex*/ false, OwnerActor);
			LosParams.AddIgnoredActor(OwnerActor);
			LosParams.AddIgnoredActor(Actor);
			const bool bBlocked = World->LineTraceSingleByChannel(Hit, View.ViewLocation, Focus, LineOfSightChannel, LosParams);
			if (bBlocked)
			{
				continue;
			}
		}

		FCam_TargetCandidate Candidate;
		Candidate.EntityId = Id;
		Candidate.Actor = Actor;
		Candidate.FocusLocation = Focus;
		Candidate.Distance = Dist;
		Candidate.AngleFromViewDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
		Candidate.ArchetypeTag = Archetype;
		OutCandidates.Add(MoveTemp(Candidate));
	}

	return OutCandidates.Num();
}

FSeam_EntityId UCam_TargetingComponent::SelectBest(const TArray<FCam_TargetCandidate>& Candidates, const FCam_TargetingView& View) const
{
	if (Candidates.Num() == 0)
	{
		return FSeam_EntityId::Invalid();
	}

	const UCam_TargetSelectionStrategy* Strategy = SelectionStrategy;

	float BestScore = -FLT_MAX;
	FSeam_EntityId BestId = FSeam_EntityId::Invalid();
	for (const FCam_TargetCandidate& Candidate : Candidates)
	{
		float Score;
		if (Strategy)
		{
			Score = Strategy->ScoreCandidate(Candidate, View);
			// Stickiness: bias the already-current target to avoid flicker between near-equal picks.
			if (Candidate.EntityId == View.CurrentTargetId && View.CurrentTargetId.IsValid())
			{
				Score *= (1.f + FMath::Max(0.f, Strategy->CurrentTargetStickinessBonus));
			}
		}
		else
		{
			// Built-in closest fallback when no strategy is authored (1 - normalized distance).
			const float Range = View.MaxRange > KINDA_SMALL_NUMBER ? View.MaxRange : 1.f;
			Score = 1.f - FMath::Clamp(Candidate.Distance / Range, 0.f, 1.f);
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestId = Candidate.EntityId;
		}
	}

	return BestScore > 0.f ? BestId : FSeam_EntityId::Invalid();
}

void UCam_TargetingComponent::RebuildCandidateRing(TArray<FCam_TargetCandidate>& Candidates, const FCam_TargetingView& View)
{
	// Sort left-to-right by signed yaw of the candidate relative to the view, so CycleTarget walks a
	// stable on-screen ordering (next = the target to the right).
	const FVector Up = FVector::UpVector;
	Candidates.Sort([&View, &Up](const FCam_TargetCandidate& A, const FCam_TargetCandidate& B)
	{
		auto SignedYaw = [&View, &Up](const FCam_TargetCandidate& C) -> float
		{
			const FVector Dir = (C.FocusLocation - View.ViewLocation).GetSafeNormal();
			const FVector Right = FVector::CrossProduct(Up, View.ViewForward).GetSafeNormal();
			const float RightComp = FVector::DotProduct(Dir, Right);
			const float FwdComp = FVector::DotProduct(Dir, View.ViewForward);
			return FMath::Atan2(RightComp, FwdComp);
		};
		return SignedYaw(A) < SignedYaw(B);
	});

	CandidateRing.Reset(Candidates.Num());
	for (const FCam_TargetCandidate& C : Candidates)
	{
		CandidateRing.Add(C.EntityId);
	}
}

//------------------------------------------------------------------------------------------------
// Internals: target change plumbing
//------------------------------------------------------------------------------------------------

void UCam_TargetingComponent::SetCurrentTarget(const FSeam_EntityId& NewId, bool bHardLock)
{
	if (NewId == CurrentTargetId)
	{
		return;
	}

	const FSeam_EntityId Previous = CurrentTargetId;
	CurrentTargetId = NewId;

	// Local delegate for in-process listeners (HUD widgets on the same client).
	OnTargetChanged.Broadcast(NewId, Previous);

	// Cosmetic bus broadcast for UI/HUD that listens by tag (DP.Bus.Camera.TargetChanged). Local only.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FCam_TargetChangedEvent Event;
		Event.NewTargetId = NewId;
		Event.PreviousTargetId = Previous;
		Event.bHardLock = bHardLock;
		Bus->BroadcastPayload(Cam_NativeTags::Bus_TargetChanged, FInstancedStruct::Make(Event), this);
	}

	// Authoritative intent: report the chosen target id to the server if this affects gameplay.
	if (bReportTargetToServer && NewId != LastReportedTargetId)
	{
		const APlayerController* PC = ResolveOwningPlayerController();
		if (PC && PC->IsLocalController())
		{
			LastReportedTargetId = NewId;
			ServerSetSoftLockTarget(NewId);
		}
	}
}

//------------------------------------------------------------------------------------------------
// Internals: resolution helpers
//------------------------------------------------------------------------------------------------

APlayerController* UCam_TargetingComponent::ResolveOwningPlayerController() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}
	if (APlayerController* AsPC = Cast<APlayerController>(OwnerActor))
	{
		return AsPC;
	}
	if (const APawn* AsPawn = Cast<APawn>(OwnerActor))
	{
		return Cast<APlayerController>(AsPawn->GetController());
	}
	return nullptr;
}

bool UCam_TargetingComponent::ReadIdentity(AActor* Actor, FSeam_EntityId& OutId, FGameplayTag& OutArchetype) const
{
	if (!Actor)
	{
		return false;
	}

	// Identity may be on the actor itself or one of its components (e.g. the Entity module's component).
	if (Actor->Implements<USeam_EntityIdentity>())
	{
		OutId = ISeam_EntityIdentity::Execute_GetEntityId(Actor);
		OutArchetype = ISeam_EntityIdentity::Execute_GetArchetypeTag(Actor);
		return OutId.IsValid();
	}

	if (UActorComponent* IdComp = Actor->FindComponentByInterface(USeam_EntityIdentity::StaticClass()))
	{
		OutId = ISeam_EntityIdentity::Execute_GetEntityId(IdComp);
		OutArchetype = ISeam_EntityIdentity::Execute_GetArchetypeTag(IdComp);
		return OutId.IsValid();
	}

	return false;
}

//------------------------------------------------------------------------------------------------
// Internals: service registration & input mode
//------------------------------------------------------------------------------------------------

void UCam_TargetingComponent::RegisterAsTargetSourceService()
{
	// Only the locally-controlled player's component is the meaningful "current target" source.
	const APlayerController* PC = ResolveOwningPlayerController();
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator must not keep this component alive past its actor's lifetime.
		Locator->RegisterService(Cam_NativeTags::Service_TargetSource, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}
}

void UCam_TargetingComponent::UnregisterTargetSourceService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// Only drop the slot if it still points at us (another local component may have overridden it).
		if (Locator->ResolveService(Cam_NativeTags::Service_TargetSource) == this)
		{
			Locator->UnregisterService(Cam_NativeTags::Service_TargetSource);
		}
	}
}

UObject* UCam_TargetingComponent::ResolveInputArbiterObject() const
{
	UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}
	// The Platform input router publishes the arbiter seam under this conventional key (same key the
	// sibling camera director resolves), anchored under DP.Service. We never touch Platform's concrete type.
	static const FGameplayTag ArbiterKey = FGameplayTag::RequestGameplayTag(FName("DP.Service.Input.ModeArbiter"), /*ErrorIfNotFound=*/false);
	if (!ArbiterKey.IsValid())
	{
		return nullptr;
	}
	UObject* Provider = Locator->ResolveService(ArbiterKey);
	return (Provider && Provider->GetClass()->ImplementsInterface(USeam_InputModeArbiter::StaticClass())) ? Provider : nullptr;
}

void UCam_TargetingComponent::PushLockOnInputMode()
{
	if (InputModeRequestId.IsValid())
	{
		return; // already pushed
	}
	if (UObject* ArbiterObj = ResolveInputArbiterObject())
	{
		InputModeRequestId = ISeam_InputModeArbiter::Execute_PushInputMode(ArbiterObj, Cam_NativeTags::InputMode_LockOn, LockOnInputModePriority);
	}
}

void UCam_TargetingComponent::PopLockOnInputMode()
{
	if (!InputModeRequestId.IsValid())
	{
		return;
	}
	if (UObject* ArbiterObj = ResolveInputArbiterObject())
	{
		ISeam_InputModeArbiter::Execute_PopInputMode(ArbiterObj, InputModeRequestId);
	}
	InputModeRequestId.Invalidate();
}

//------------------------------------------------------------------------------------------------
// Net: client -> server intent
//------------------------------------------------------------------------------------------------

bool UCam_TargetingComponent::ServerSetSoftLockTarget_Validate(FSeam_EntityId TargetId)
{
	// Accept Invalid (clearing the target) outright; non-trivial validation happens in _Implementation
	// where we have world access to re-gather candidates. Reject only obviously malformed input here.
	return true;
}

void UCam_TargetingComponent::ServerSetSoftLockTarget_Implementation(FSeam_EntityId TargetId)
{
	// Authority guard at the TOP of the mutator.
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	// Clearing is always allowed.
	if (!TargetId.IsValid())
	{
		ServerAcceptedTargetId = FSeam_EntityId::Invalid();
		return;
	}

	// Server-side validation: the reported id must currently be a real, in-range, in-cone candidate.
	// We rebuild the view/candidates authoritatively (the server runs the same gather) and reject ids
	// the client could not legitimately see, preventing spoofed/aim-bot target injection.
	FCam_TargetingView View;
	if (!BuildView(View))
	{
		return;
	}
	TArray<FCam_TargetCandidate> Candidates;
	GatherCandidates(View, Candidates);

	const bool bValid = Candidates.ContainsByPredicate(
		[&TargetId](const FCam_TargetCandidate& C) { return C.EntityId == TargetId; });
	if (!bValid)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Cam_Targeting] %s: server rejected soft-lock target %s (not a valid candidate)."),
			*GetNameSafe(OwnerActor), *TargetId.ToString());
		return;
	}

	ServerAcceptedTargetId = TargetId;
	UE_LOG(LogDP, Verbose, TEXT("[Cam_Targeting] %s: server accepted soft-lock target %s."),
		*GetNameSafe(OwnerActor), *TargetId.ToString());
}
