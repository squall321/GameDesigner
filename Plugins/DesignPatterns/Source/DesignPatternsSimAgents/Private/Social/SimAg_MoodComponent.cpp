// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Social/SimAg_MoodComponent.h"
#include "Clock/SimAg_ClockSubsystem.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Clock/Seam_SimClock.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

//~ FSimAg_Emotion fast-array callbacks (clients only) --------------------------------------------

void FSimAg_Emotion::PostReplicatedAdd(const FSimAg_MoodArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimAg_Emotion::PostReplicatedChange(const FSimAg_MoodArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimAg_Emotion::PreReplicatedRemove(const FSimAg_MoodArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

//~ USimAg_MoodComponent --------------------------------------------------------------------------

USimAg_MoodComponent::USimAg_MoodComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	Mood.OwnerComponent = this;
}

void USimAg_MoodComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		ReplicationCadence = FMath::Max(0.05f, Settings->MoodReplicationCadence);
	}

	Mood.OwnerComponent = this;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Mood.Emotions.Reset();
		for (const FSimAg_Emotion& Default : DefaultEmotions)
		{
			if (!Default.Axis.IsValid())
			{
				continue;
			}
			FSimAg_Emotion Emotion = Default;
			Emotion.Intensity = FMath::Clamp(Emotion.Intensity, 0.f, 1.f);
			Emotion.Baseline = FMath::Clamp(Emotion.Baseline, 0.f, 1.f);
			Mood.Emotions.Add(Emotion);
		}
		Mood.MarkArrayDirty();
	}
}

void USimAg_MoodComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	DecayMood(DeltaTime);

	ReplicationAccumulator += DeltaTime;
	if (ReplicationAccumulator >= ReplicationCadence)
	{
		ReplicationAccumulator = 0.f;
		for (FSimAg_Emotion& Emotion : Mood.Emotions)
		{
			Mood.MarkItemDirty(Emotion);
		}
	}
}

void USimAg_MoodComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimAg_MoodComponent, Mood);
}

//~ ISeam_MoodProvider ----------------------------------------------------------------------------

float USimAg_MoodComponent::GetMoodNormalized_Implementation(FGameplayTag MoodTag) const
{
	const FSimAg_Emotion* Emotion = FindAxis(MoodTag);
	// Neutral 0.5 when the axis is unknown, per the seam's documented contract.
	return Emotion ? FMath::Clamp(Emotion->Intensity, 0.f, 1.f) : 0.5f;
}

float USimAg_MoodComponent::GetNeedWeightMultiplier_Implementation(FGameplayTag NeedTag) const
{
	float Multiplier = 1.f;
	for (const FSimAg_MoodNeedInfluence& Rule : NeedInfluences)
	{
		if (!NeedTag.MatchesTag(Rule.Need))
		{
			continue;
		}
		const FSimAg_Emotion* Emotion = FindAxis(Rule.Axis);
		if (!Emotion)
		{
			continue;
		}
		// Lerp from 1.0 (no influence) at intensity 0 to WeightAtFullIntensity at intensity 1.
		const float Intensity = FMath::Clamp(Emotion->Intensity, 0.f, 1.f);
		const float Factor = FMath::Lerp(1.f, Rule.WeightAtFullIntensity, Intensity);
		Multiplier *= Factor;
	}
	return FMath::Max(0.f, Multiplier);
}

//~ Mutators (authority only) ---------------------------------------------------------------------

float USimAg_MoodComponent::ApplyMoodDelta(FGameplayTag Axis, float Delta)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		const FSimAg_Emotion* Emotion = FindAxis(Axis);
		return Emotion ? Emotion->Intensity : 0.5f;
	}
	FSimAg_Emotion* Emotion = FindAxis(Axis);
	if (!Emotion)
	{
		return 0.5f;
	}
	Emotion->Intensity = FMath::Clamp(Emotion->Intensity + Delta, 0.f, 1.f);
	MarkAxisDirty(*Emotion, /*bEmitBus*/ true);
	return Emotion->Intensity;
}

void USimAg_MoodComponent::SetMood(FGameplayTag Axis, float NewIntensity)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	FSimAg_Emotion* Emotion = FindAxis(Axis);
	if (!Emotion)
	{
		return;
	}
	Emotion->Intensity = FMath::Clamp(NewIntensity, 0.f, 1.f);
	MarkAxisDirty(*Emotion, /*bEmitBus*/ true);
}

void USimAg_MoodComponent::AddOrUpdateAxis(const FSimAg_Emotion& InEmotion)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!InEmotion.Axis.IsValid())
	{
		return;
	}
	if (FSimAg_Emotion* Existing = FindAxis(InEmotion.Axis))
	{
		*Existing = InEmotion;
		Existing->Intensity = FMath::Clamp(Existing->Intensity, 0.f, 1.f);
		Existing->Baseline = FMath::Clamp(Existing->Baseline, 0.f, 1.f);
		MarkAxisDirty(*Existing, /*bEmitBus*/ false);
	}
	else
	{
		FSimAg_Emotion Emotion = InEmotion;
		Emotion.Intensity = FMath::Clamp(Emotion.Intensity, 0.f, 1.f);
		Emotion.Baseline = FMath::Clamp(Emotion.Baseline, 0.f, 1.f);
		FSimAg_Emotion& Added = Mood.Emotions.Add_GetRef(Emotion);
		MarkAxisDirty(Added, /*bEmitBus*/ false);
	}
}

//~ Reads (client-safe) ---------------------------------------------------------------------------

float USimAg_MoodComponent::GetIntensity(FGameplayTag Axis) const
{
	const FSimAg_Emotion* Emotion = FindAxis(Axis);
	return Emotion ? Emotion->Intensity : 0.5f;
}

void USimAg_MoodComponent::HandleReplicatedChange()
{
	OnMoodChanged.Broadcast(this);
}

//~ Internals -------------------------------------------------------------------------------------

FSimAg_Emotion* USimAg_MoodComponent::FindAxis(const FGameplayTag& Axis)
{
	return Mood.Emotions.FindByPredicate([&Axis](const FSimAg_Emotion& E) { return E.Axis == Axis; });
}

const FSimAg_Emotion* USimAg_MoodComponent::FindAxis(const FGameplayTag& Axis) const
{
	return Mood.Emotions.FindByPredicate([&Axis](const FSimAg_Emotion& E) { return E.Axis == Axis; });
}

void USimAg_MoodComponent::DecayMood(float DeltaSeconds)
{
	const float Scale = GetClockTimeScale();
	if (Scale <= 0.f)
	{
		return; // paused / stopped sim time => no decay
	}
	const float Scaled = DeltaSeconds * Scale;

	bool bAnyChanged = false;
	for (FSimAg_Emotion& Emotion : Mood.Emotions)
	{
		if (Emotion.DecayPerSecond <= 0.f)
		{
			continue;
		}
		const float Step = Emotion.DecayPerSecond * Scaled;
		const float Before = Emotion.Intensity;
		// Move toward Baseline without overshooting.
		if (Emotion.Intensity > Emotion.Baseline)
		{
			Emotion.Intensity = FMath::Max(Emotion.Baseline, Emotion.Intensity - Step);
		}
		else if (Emotion.Intensity < Emotion.Baseline)
		{
			Emotion.Intensity = FMath::Min(Emotion.Baseline, Emotion.Intensity + Step);
		}
		if (!FMath::IsNearlyEqual(Before, Emotion.Intensity))
		{
			bAnyChanged = true;
		}
	}
	if (bAnyChanged)
	{
		OnMoodChanged.Broadcast(this);
	}
}

void USimAg_MoodComponent::MarkAxisDirty(FSimAg_Emotion& Emotion, bool bEmitBus)
{
	Mood.MarkItemDirty(Emotion);
	OnMoodChanged.Broadcast(this);

	if (bEmitBus)
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FSimAg_MoodEvent Event;
			Event.Axis = Emotion.Axis;
			Event.Intensity = Emotion.Intensity;

			FInstancedStruct Payload;
			Payload.InitializeAs<FSimAg_MoodEvent>(Event);
			Bus->BroadcastPayload(SimAgNativeTags::Bus_MoodChanged, Payload, GetOwner());
		}
	}
}

float USimAg_MoodComponent::GetClockTimeScale() const
{
	UObject* ClockObj = GetClock();
	if (ClockObj && ClockObj->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
	{
		if (ISeam_SimClock::Execute_IsPaused(ClockObj))
		{
			return 0.f;
		}
		return static_cast<float>(ISeam_SimClock::Execute_GetTimeScale(ClockObj));
	}
	return 1.f;
}

USimAg_ClockSubsystem* USimAg_MoodComponent::GetClock() const
{
	if (CachedClock.IsValid())
	{
		return CachedClock.Get();
	}
	USimAg_MoodComponent* MutableThis = const_cast<USimAg_MoodComponent*>(this);

	UObject* ClockObj = nullptr;
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		ClockObj = Locator->ResolveService(SimAgNativeTags::Service_Clock);
	}
	USimAg_ClockSubsystem* Clock = Cast<USimAg_ClockSubsystem>(ClockObj);
	if (!Clock)
	{
		Clock = FDP_SubsystemStatics::GetWorldSubsystem<USimAg_ClockSubsystem>(this);
	}
	MutableThis->CachedClock = Clock;
	return Clock;
}
