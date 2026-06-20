// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Sequence/Narr_SequenceDirectorComponent.h"
#include "Story/Narr_StoryNativeTags.h"

#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

// Input-mode arbiter seam (resolved from the locator; never a hard module dep on Platform).
#include "Input/Seam_InputModeArbiter.h"

// World hub (PRIVATE) for the authoritative completion flag.
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_Scope.h"

// Engine sequence player.
#include "LevelSequence.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"

#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

// =============================================================================================
//  UNarr_SequenceDirectorComponent
// =============================================================================================

UNarr_SequenceDirectorComponent::UNarr_SequenceDirectorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Cosmetic/local: never replicated. Each machine plays its own cutscene from replicated gameplay.
	SetIsReplicatedByDefault(false);
}

bool UNarr_SequenceDirectorComponent::Play(ULevelSequence* Sequence, FGameplayTag SequenceTag, const FNarr_SequencePlayParams& Params)
{
	if (!Sequence)
	{
		UE_LOG(LogDP, Warning, TEXT("[Narr] SequenceDirector::Play with null sequence on '%s'."), *GetNameSafe(GetOwner()));
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Stop any in-flight playback first (counts as aborted).
	if (bIsPlaying)
	{
		EndPlayback(ENarr_SequenceEndReason::Aborted);
	}

	ALevelSequenceActor* OutActor = nullptr;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(
		World, Sequence, Params.PlaybackSettings, OutActor);
	if (!Player)
	{
		UE_LOG(LogDP, Warning, TEXT("[Narr] Failed to create level sequence player for '%s'."), *Sequence->GetName());
		return false;
	}

	SequencePlayer = Player;
	SequenceActor = OutActor;
	ActiveSequenceTag = SequenceTag;
	ActiveParams = Params;
	bIsPlaying = true;

	// Bind completion BEFORE Play so a zero-length sequence still routes through our teardown.
	SequencePlayer->OnFinished.AddDynamic(this, &UNarr_SequenceDirectorComponent::HandleSequenceFinished);

	AcquireInputLock();

	SequencePlayer->Play();

	UE_LOG(LogDP, Log, TEXT("[Narr] Cutscene '%s' started (skippable=%s, inputLock=%s)."),
		*SequenceTag.ToString(),
		Params.bSkippable ? TEXT("yes") : TEXT("no"),
		Params.bLockInput ? TEXT("yes") : TEXT("no"));

	OnSequenceStarted.Broadcast(SequenceTag);
	BroadcastSequenceEvent(NarrativeStoryNativeTags::Bus_Narrative_Sequence_Started, ENarr_SequenceEndReason::Finished);
	return true;
}

bool UNarr_SequenceDirectorComponent::PlayDefault()
{
	return Play(DefaultSequence, DefaultSequenceTag, DefaultPlayParams);
}

bool UNarr_SequenceDirectorComponent::Skip()
{
	if (!bIsPlaying || !ActiveParams.bSkippable)
	{
		return false;
	}
	UE_LOG(LogDP, Log, TEXT("[Narr] Cutscene '%s' skipped."), *ActiveSequenceTag.ToString());

	// Stop the engine player without re-entering via OnFinished, then end with the Skipped reason.
	if (SequencePlayer)
	{
		SequencePlayer->OnFinished.RemoveDynamic(this, &UNarr_SequenceDirectorComponent::HandleSequenceFinished);
		SequencePlayer->Stop();
	}
	EndPlayback(ENarr_SequenceEndReason::Skipped);
	return true;
}

void UNarr_SequenceDirectorComponent::Stop()
{
	if (!bIsPlaying)
	{
		return;
	}
	if (SequencePlayer)
	{
		SequencePlayer->OnFinished.RemoveDynamic(this, &UNarr_SequenceDirectorComponent::HandleSequenceFinished);
		SequencePlayer->Stop();
	}
	EndPlayback(ENarr_SequenceEndReason::Aborted);
}

void UNarr_SequenceDirectorComponent::HandleSequenceFinished()
{
	if (!bIsPlaying)
	{
		return;
	}
	EndPlayback(ENarr_SequenceEndReason::Finished);
}

void UNarr_SequenceDirectorComponent::EndPlayback(ENarr_SequenceEndReason Reason)
{
	if (!bIsPlaying)
	{
		return;
	}

	const FGameplayTag EndedTag = ActiveSequenceTag;

	// Release the input lock first so input is restored even if later steps early-out.
	ReleaseInputLock();

	// Authoritative side-effect: record completion in the world hub per policy.
	TrySetCompletionFlag(Reason);

	bIsPlaying = false;

	// Broadcast the appropriate bus channel + the local delegate.
	const FGameplayTag Channel = (Reason == ENarr_SequenceEndReason::Skipped)
		? NarrativeStoryNativeTags::Bus_Narrative_Sequence_Skipped
		: NarrativeStoryNativeTags::Bus_Narrative_Sequence_Finished;
	BroadcastSequenceEvent(Channel, Reason);
	OnSequenceEnded.Broadcast(EndedTag, Reason);

	DestroyPlayer();
	ActiveSequenceTag = FGameplayTag();
	ActiveParams = FNarr_SequencePlayParams();

	UE_LOG(LogDP, Log, TEXT("[Narr] Cutscene '%s' ended (reason=%d)."), *EndedTag.ToString(), static_cast<int32>(Reason));
}

void UNarr_SequenceDirectorComponent::AcquireInputLock()
{
	if (!ActiveParams.bLockInput)
	{
		return;
	}

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}

	// The Platform module registers its input router as the arbiter under DP.Service.InputModeArbiter.
	UObject* Provider = Locator->ResolveService(
		FGameplayTag::RequestGameplayTag(TEXT("DP.Service.InputModeArbiter"), /*ErrorIfNotFound=*/false));
	if (!Provider || !Provider->Implements<USeam_InputModeArbiter>())
	{
		UE_LOG(LogDP, Verbose, TEXT("[Narr] No input-mode arbiter registered; cutscene plays without input lock."));
		return;
	}

	InputArbiter = TWeakInterfacePtr<ISeam_InputModeArbiter>(Cast<ISeam_InputModeArbiter>(Provider));
	InputLockRequest = ISeam_InputModeArbiter::Execute_PushInputMode(
		Provider, NarrativeStoryNativeTags::InputMode_Cutscene, ActiveParams.InputLockPriority);
}

void UNarr_SequenceDirectorComponent::ReleaseInputLock()
{
	if (!InputLockRequest.IsValid())
	{
		return;
	}
	if (UObject* ArbiterObj = InputArbiter.GetObject())
	{
		ISeam_InputModeArbiter::Execute_PopInputMode(ArbiterObj, InputLockRequest);
	}
	InputLockRequest.Invalidate();
	InputArbiter.Reset();
}

void UNarr_SequenceDirectorComponent::TrySetCompletionFlag(ENarr_SequenceEndReason Reason)
{
	if (!ActiveParams.CompletionHubFlag.IsValid())
	{
		return;
	}
	// Aborted playback never counts as "seen". Skipped counts only when policy allows.
	if (Reason == ENarr_SequenceEndReason::Aborted)
	{
		return;
	}
	if (Reason == ENarr_SequenceEndReason::Skipped && !ActiveParams.bCompletionFlagOnSkip)
	{
		return;
	}

	// AUTHORITY ONLY: the hub's SetFlag no-ops on clients, so this is safe to call from any machine.
	if (UWorldHub_StateHubSubsystem* Hub =
		FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this))
	{
		Hub->SetFlag(ActiveParams.CompletionHubFlag, true, FWorldHub_Scope::Global());
	}
}

void UNarr_SequenceDirectorComponent::BroadcastSequenceEvent(const FGameplayTag& Channel, ENarr_SequenceEndReason Reason) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus || !Channel.IsValid())
	{
		return;
	}
	const FNarr_SequenceEventPayload Payload(ActiveSequenceTag, Reason);
	FInstancedStruct Wrapped = FInstancedStruct::Make(Payload);
	Bus->BroadcastPayload(Channel, Wrapped, const_cast<UNarr_SequenceDirectorComponent*>(this));
}

void UNarr_SequenceDirectorComponent::DestroyPlayer()
{
	if (SequencePlayer)
	{
		SequencePlayer->OnFinished.RemoveDynamic(this, &UNarr_SequenceDirectorComponent::HandleSequenceFinished);
		SequencePlayer = nullptr;
	}
	if (SequenceActor)
	{
		// The engine spawns a transient ALevelSequenceActor to back the player; destroy it so it does
		// not linger in the world after the cutscene.
		SequenceActor->Destroy();
		SequenceActor = nullptr;
	}
}

void UNarr_SequenceDirectorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsPlaying)
	{
		// Stop cleanly so the input lock is released and the engine player is torn down.
		Stop();
	}
	else
	{
		ReleaseInputLock();
		DestroyPlayer();
	}
	Super::EndPlay(EndPlayReason);
}

// =============================================================================================
//  UNarr_SequenceTriggerComponent
// =============================================================================================

UNarr_SequenceTriggerComponent::UNarr_SequenceTriggerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UNarr_SequenceTriggerComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveDirector();

	// Bind to the owner's primitive overlap so the trigger fires on volume entry. We bind to the root
	// primitive (a trigger volume's collision component), wrapping the engine overlap event.
	if (AActor* Owner = GetOwner())
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
		{
			Prim->OnComponentBeginOverlap.AddDynamic(this, &UNarr_SequenceTriggerComponent::HandleActorOverlap);
		}
		else
		{
			UE_LOG(LogDP, Verbose, TEXT("[Narr] SequenceTrigger on '%s' has no primitive root; overlap binding skipped."),
				*GetNameSafe(Owner));
		}
	}
}

void UNarr_SequenceTriggerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* Owner = GetOwner())
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
		{
			Prim->OnComponentBeginOverlap.RemoveDynamic(this, &UNarr_SequenceTriggerComponent::HandleActorOverlap);
		}
	}
	Super::EndPlay(EndPlayReason);
}

UNarr_SequenceDirectorComponent* UNarr_SequenceTriggerComponent::ResolveDirector()
{
	if (Director)
	{
		return Director;
	}
	if (AActor* Owner = GetOwner())
	{
		Director = Owner->FindComponentByClass<UNarr_SequenceDirectorComponent>();
	}
	return Director;
}

bool UNarr_SequenceTriggerComponent::DoesActorQualify(const AActor* OtherActor) const
{
	if (!OtherActor)
	{
		return false;
	}
	if (!bRequireLocalPlayerPawn)
	{
		return true;
	}
	// Only a locally-controlled player pawn qualifies, so cosmetic cutscenes fire on the right machine.
	if (const APawn* Pawn = Cast<APawn>(OtherActor))
	{
		return Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled();
	}
	return false;
}

void UNarr_SequenceTriggerComponent::HandleActorOverlap(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*Sweep*/)
{
	if (!bArmed || !DoesActorQualify(OtherActor))
	{
		return;
	}

	UNarr_SequenceDirectorComponent* Dir = ResolveDirector();
	if (!Dir)
	{
		UE_LOG(LogDP, Warning, TEXT("[Narr] SequenceTrigger on '%s' has no director to drive."), *GetNameSafe(GetOwner()));
		return;
	}

	if (Dir->PlayDefault() && bFireOnce)
	{
		bArmed = false;
	}
}
