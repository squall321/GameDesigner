// Copyright DesignPatterns plugin. All Rights Reserved.

#include "SampleGameInstance.h"

#include "Core/DPSubsystemLibrary.h"
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_Scope.h"
#include "Slot/SaveX_SlotManagerSubsystem.h"
#include "Persist/Seam_SaveSlotManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDPSample, Log, All);

void USampleGameInstance::Init()
{
	Super::Init();

	UE_LOG(LogDPSample, Log, TEXT("=== DesignPatterns sample: composing plugin modules ==="));

	DemoWorldHub();
	DemoSaveSlots();

	UE_LOG(LogDPSample, Log, TEXT("=== sample composition complete ==="));
}

void USampleGameInstance::DemoWorldHub()
{
	// The World hub is a World subsystem; it exists once a world is initialised. At GameInstance::Init
	// time there may be no world yet, so resolve defensively and bail cleanly if absent. (A real game
	// would drive these from gameplay, where a world is always present.)
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogDPSample, Log, TEXT("[WorldHub] no world yet at Init — skipping hub demo (expected)."));
		return;
	}

	UWorldHub_StateHubSubsystem* Hub = World->GetSubsystem<UWorldHub_StateHubSubsystem>();
	if (!Hub)
	{
		UE_LOG(LogDPSample, Warning, TEXT("[WorldHub] hub subsystem unavailable."));
		return;
	}

	const FWorldHub_Scope Scope = FWorldHub_Scope::Global();

	// Write some game-wide state through the hub (authority-guarded inside the hub).
	if (TutorialSeenFlagTag.IsValid())
	{
		Hub->SetFlag(TutorialSeenFlagTag, true, Scope);
	}
	if (EnemiesDefeatedCounterTag.IsValid())
	{
		Hub->IncrementCounter(EnemiesDefeatedCounterTag, 3, Scope);
		Hub->IncrementCounter(EnemiesDefeatedCounterTag, 2, Scope);
	}

	// Read it back — any system (quests, achievements, UI) reads the same hub the same way.
	const bool bTutorialSeen = TutorialSeenFlagTag.IsValid() && Hub->QueryFlag(TutorialSeenFlagTag, Scope);
	const int64 Defeated = EnemiesDefeatedCounterTag.IsValid() ? Hub->QueryCounter(EnemiesDefeatedCounterTag, Scope) : 0;

	UE_LOG(LogDPSample, Log, TEXT("[WorldHub] tutorialSeen=%s enemiesDefeated=%lld"),
		bTutorialSeen ? TEXT("true") : TEXT("false"), Defeated);
}

void USampleGameInstance::DemoSaveSlots()
{
	USaveX_SlotManagerSubsystem* Slots = GetSubsystem<USaveX_SlotManagerSubsystem>();
	if (!Slots)
	{
		UE_LOG(LogDPSample, Warning, TEXT("[SaveSlots] slot manager unavailable."));
		return;
	}

	TArray<FSeam_SaveSlotInfo> SlotInfos;
	Slots->ListSlots(SlotInfos);

	// "Most recent" is exposed through the ISeam_SaveSlotManager seam — call it the same way any
	// decoupled consumer (e.g. a "Continue" button) would, via the interface Execute_ wrapper.
	const FString MostRecent = ISeam_SaveSlotManager::Execute_GetMostRecentSlot(Slots);

	UE_LOG(LogDPSample, Log, TEXT("[SaveSlots] %d existing slot(s); most-recent='%s'"),
		SlotInfos.Num(), *MostRecent);

	for (const FSeam_SaveSlotInfo& Info : SlotInfos)
	{
		UE_LOG(LogDPSample, Log, TEXT("[SaveSlots]   '%s' display='%s' playtime=%.0fs"),
			*Info.SlotName, *Info.DisplayName.ToString(), Info.PlaytimeSeconds);
	}
}
