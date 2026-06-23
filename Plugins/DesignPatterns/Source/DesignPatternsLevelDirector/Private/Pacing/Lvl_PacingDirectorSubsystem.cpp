// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pacing/Lvl_PacingDirectorSubsystem.h"
#include "Pacing/Lvl_TensionCurveDataAsset.h"
#include "Pacing/Lvl_PacingBusPayloads.h"
#include "Lvl_BusPayloads.h"
#include "DesignPatternsLevelDirectorNativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"

// The shared encounter-director seam (resolved from the locator; no AI hard-include).
#include "AI/Seam_EncounterDirector.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Engine/World.h"

void ULvl_PacingDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LastTension = 0.f;

	// Listen for encounter (de)activation so pacing follows the activator's region lifecycle. The bus is
	// local; these handlers run on every machine, but every mutation they trigger is authority-gated.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		ActivatedListener = Bus->ListenNative(LvlNativeTags::Bus_Lvl_Encounter_Activated,
			[this](const FDP_Message& Msg) { HandleEncounterActivated(Msg); }, this);
		DeactivatedListener = Bus->ListenNative(LvlNativeTags::Bus_Lvl_Encounter_Deactivated,
			[this](const FDP_Message& Msg) { HandleEncounterDeactivated(Msg); }, this);
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ULvl_PacingDirectorSubsystem::TickPacing));

	UE_LOG(LogDP, Log, TEXT("[LevelDirector] Pacing director initialized (authority=%s)."),
		HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}

void ULvl_PacingDirectorSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// Remove our bus listeners (rule 3: every listener removed on teardown).
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		if (ActivatedListener.IsValid()) { Bus->StopListening(ActivatedListener); }
		if (DeactivatedListener.IsValid()) { Bus->StopListening(DeactivatedListener); }
	}
	ActivatedListener = FDP_ListenerHandle();
	DeactivatedListener = FDP_ListenerHandle();

	// Stop any still-active encounters we own (authority only).
	if (HasWorldAuthority())
	{
		TScriptInterface<ISeam_EncounterDirector> Director = ResolveEncounterDirector();
		if (Director.GetObject())
		{
			for (const TPair<FGameplayTag, FPacedRegion>& Pair : PacedRegions)
			{
				if (Pair.Value.bActivated)
				{
					ISeam_EncounterDirector::Execute_StopEncounter(Director.GetObject(), Pair.Key);
				}
			}
		}
	}

	PacedRegions.Reset();
	PacedCurves.Reset();
	CachedEncounterDirector.Reset();

	Super::Deinitialize();
}

TScriptInterface<ISeam_EncounterDirector> ULvl_PacingDirectorSubsystem::ResolveEncounterDirector() const
{
	// Re-resolve each use (the adapter is a world-lifetime object; we never cache a strong ref).
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	TScriptInterface<ISeam_EncounterDirector> Result;
	if (!Locator)
	{
		return Result; // unresolved -> pacing no-ops
	}
	UObject* Obj = Locator->ResolveService(LvlNativeTags::Service_Lvl_EncounterDirector);
	if (Obj && Obj->GetClass()->ImplementsInterface(USeam_EncounterDirector::StaticClass()))
	{
		Result.SetObject(Obj);
		Result.SetInterface(Cast<ISeam_EncounterDirector>(Obj));
		CachedEncounterDirector = TWeakInterfacePtr<ISeam_EncounterDirector>(Cast<ISeam_EncounterDirector>(Obj));
	}
	return Result;
}

bool ULvl_PacingDirectorSubsystem::BeginPacing(FGameplayTag RegionTag, ULvl_TensionCurveDataAsset* Curve)
{
	// AUTHORITY GUARD AT THE TOP — clients never pace.
	if (!HasWorldAuthority())
	{
		return false;
	}
	if (!RegionTag.IsValid() || !Curve)
	{
		return false;
	}

	FPacedRegion& State = PacedRegions.FindOrAdd(RegionTag);
	State.Curve = Curve;
	State.ElapsedSeconds = 0.f;
	State.LastTension = 0.f;
	State.Band = EPacingBand::Mid;
	State.bActivated = false;
	PacedCurves.Add(RegionTag, Curve); // keep the curve alive while we pace

	UE_LOG(LogDP, Verbose, TEXT("[LevelDirector] Began pacing region %s with curve %s."),
		*RegionTag.ToString(), *Curve->DataTag.ToString());
	return true;
}

bool ULvl_PacingDirectorSubsystem::EndPacing(FGameplayTag RegionTag)
{
	// AUTHORITY GUARD AT THE TOP.
	if (!HasWorldAuthority())
	{
		return false;
	}
	FPacedRegion* State = PacedRegions.Find(RegionTag);
	if (!State)
	{
		return false;
	}

	if (State->bActivated)
	{
		if (TScriptInterface<ISeam_EncounterDirector> Director = ResolveEncounterDirector(); Director.GetObject())
		{
			ISeam_EncounterDirector::Execute_StopEncounter(Director.GetObject(), RegionTag);
		}
	}

	PacedRegions.Remove(RegionTag);
	PacedCurves.Remove(RegionTag);
	return true;
}

ULvl_PacingDirectorSubsystem::EPacingBand ULvl_PacingDirectorSubsystem::ClassifyBand(
	float Tension, float RelaxThreshold, float EscalateThreshold, EPacingBand CurrentBand)
{
	// Hysteresis: only escalate at/above EscalateThreshold, only relax at/below RelaxThreshold; in the
	// band keep the current band (prevents thrashing the AI director).
	if (Tension >= EscalateThreshold)
	{
		return EPacingBand::Escalated;
	}
	if (Tension <= RelaxThreshold)
	{
		return EPacingBand::Relaxed;
	}
	return (CurrentBand == EPacingBand::Mid) ? EPacingBand::Mid : CurrentBand;
}

void ULvl_PacingDirectorSubsystem::StepRegion(FGameplayTag RegionTag, FPacedRegion& State, float Dt)
{
	ULvl_TensionCurveDataAsset* Curve = State.Curve.Get();
	if (!Curve)
	{
		return; // curve GC'd; the region will be pruned by EndPacing or next tick
	}

	const float Duration = Curve->GetEffectiveDuration();
	State.ElapsedSeconds += Dt;

	float NormTime;
	if (Curve->bLoop)
	{
		NormTime = FMath::Fmod(State.ElapsedSeconds, Duration) / Duration;
	}
	else
	{
		NormTime = FMath::Clamp(State.ElapsedSeconds / Duration, 0.f, 1.f);
	}

	const float Tension = Curve->SampleTension(NormTime);
	State.LastTension = Tension;
	LastTension = Tension;

	float Relax, Escalate;
	Curve->GetClampedThresholds(Relax, Escalate);
	const EPacingBand NewBand = ClassifyBand(Tension, Relax, Escalate, State.Band);

	// Re-activate only on a band crossing (the AI director cannot mutate live intensity), or on the very
	// first evaluation so the encounter gets its initial ProgressionInput.
	const bool bCrossed = (NewBand != State.Band);
	if (!bCrossed && State.bActivated)
	{
		return;
	}

	const float Progression = Curve->TensionToProgressionInput(Tension);

	TScriptInterface<ISeam_EncounterDirector> Director = ResolveEncounterDirector();
	if (Director.GetObject())
	{
		const bool bOk = ISeam_EncounterDirector::Execute_ActivateEncounterForRegion(
			Director.GetObject(), RegionTag, Curve->EncounterId, Progression);
		if (bOk)
		{
			State.bActivated = true;
		}
	}

	if (bCrossed)
	{
		const bool bEscalated = (NewBand == EPacingBand::Escalated)
			|| (State.Band == EPacingBand::Relaxed && NewBand == EPacingBand::Mid);
		BroadcastPacingEvent(bEscalated, RegionTag, Curve->EncounterId, Tension, Progression);
	}

	State.Band = NewBand;
}

bool ULvl_PacingDirectorSubsystem::TickPacing(float Dt)
{
	// Authority-only — clients hold the listeners (for symmetry) but never advance pacing.
	if (!HasWorldAuthority() || PacedRegions.Num() == 0)
	{
		return true;
	}

	// Dt is the frame delta from FTSTicker; pacing integrates ElapsedSeconds += Dt and tolerates
	// per-frame sampling, so no separate cadence accumulator is needed here.
	// Prune regions whose curve was GC'd, then step the rest.
	TArray<FGameplayTag> Dead;
	for (TPair<FGameplayTag, FPacedRegion>& Pair : PacedRegions)
	{
		if (!Pair.Value.Curve.IsValid())
		{
			Dead.Add(Pair.Key);
			continue;
		}
		StepRegion(Pair.Key, Pair.Value, Dt);
	}
	for (const FGameplayTag& Tag : Dead)
	{
		PacedRegions.Remove(Tag);
		PacedCurves.Remove(Tag);
	}

	return true; // keep ticking
}

void ULvl_PacingDirectorSubsystem::HandleEncounterActivated(const FDP_Message& Message)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	const FLvl_EncounterEventPayload* Payload = Message.Payload.GetPtr<FLvl_EncounterEventPayload>();
	if (!Payload || !Payload->RegionTag.IsValid())
	{
		return;
	}
	// If a curve was pre-registered for this region (via BeginPacing) we already pace it. Otherwise this
	// is informational — a project can call BeginPacing in response to its own curve lookup. We do not
	// auto-invent a curve here (no magic data), keeping the activator/pacing coupling explicit.
	UE_LOG(LogDP, Verbose, TEXT("[LevelDirector] Encounter activated for region %s (paced=%s)."),
		*Payload->RegionTag.ToString(), PacedRegions.Contains(Payload->RegionTag) ? TEXT("yes") : TEXT("no"));
}

void ULvl_PacingDirectorSubsystem::HandleEncounterDeactivated(const FDP_Message& Message)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	const FLvl_EncounterEventPayload* Payload = Message.Payload.GetPtr<FLvl_EncounterEventPayload>();
	if (Payload && Payload->RegionTag.IsValid())
	{
		EndPacing(Payload->RegionTag);
	}
}

void ULvl_PacingDirectorSubsystem::BroadcastPacingEvent(bool bEscalated, FGameplayTag RegionTag,
	FGameplayTag EncounterId, float Tension, float ProgressionInput) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}
	FLvl_PacingEventPayload Payload;
	Payload.RegionTag = RegionTag;
	Payload.EncounterId = EncounterId;
	Payload.Tension = Tension;
	Payload.ProgressionInput = ProgressionInput;
	Payload.bEscalated = bEscalated;

	const FGameplayTag Channel = bEscalated
		? LvlNativeTags::Bus_Lvl_Pacing_Escalated
		: LvlNativeTags::Bus_Lvl_Pacing_Relaxed;
	Bus->BroadcastPayload(Channel, FInstancedStruct::Make(Payload),
		const_cast<ULvl_PacingDirectorSubsystem*>(this));
}

FString ULvl_PacingDirectorSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("PacingDirector regions=%d tension=%.2f director=%s"),
		PacedRegions.Num(), LastTension,
		CachedEncounterDirector.IsValid() ? TEXT("resolved") : TEXT("none"));
}
