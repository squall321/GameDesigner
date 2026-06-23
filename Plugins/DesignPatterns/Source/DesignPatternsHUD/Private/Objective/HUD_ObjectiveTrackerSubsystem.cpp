// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Objective/HUD_ObjectiveTrackerSubsystem.h"

#include "Objective/HUD_ObjectiveTrackerViewModel.h"
#include "Objective/HUD_ObjectiveTrackable.h"
#include "Minimap/HUD_MarkerRegistrySubsystem.h"
#include "HUD_NativeTags.h"
#include "HUD_DeepNativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"

void UHUD_ObjectiveTrackerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ViewModel = NewObject<UHUD_ObjectiveTrackerViewModel>(this);
	WorldMarkerTag = HUDTags::Marker_Objective;

	// Refresh eagerly on objective-change signals.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->ListenNative(HUDTags::Bus_HUD_ObjectiveChanged,
			[this](const FDP_Message& Message) { HandleObjectiveBus(Message); },
			this, EDP_MessageMatch::ExactOrChild);
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHUD_ObjectiveTrackerSubsystem::TickTracker));
}

void UHUD_ObjectiveTrackerSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}

	// Unregister every owned trackable bridge from the world registry, then drop them.
	if (UHUD_MarkerRegistrySubsystem* Registry = ResolveRegistry())
	{
		for (auto& Pair : WorldMarkers)
		{
			if (Pair.Value)
			{
				Registry->UnregisterTrackable(TScriptInterface<IHUD_Trackable>(Pair.Value));
			}
		}
	}
	WorldMarkers.Reset();

	ViewModel = nullptr;
	ObjectiveSource.Reset();
	PinnedIds.Reset();

	Super::Deinitialize();
}

void UHUD_ObjectiveTrackerSubsystem::SetObjectiveSource(const TScriptInterface<ISeam_ObjectiveSource>& InSource)
{
	if (UObject* Obj = InSource.GetObject())
	{
		ObjectiveSource = TWeakInterfacePtr<ISeam_ObjectiveSource>(*Obj);
	}
	else
	{
		ObjectiveSource.Reset();
	}
	Refresh();
}

void UHUD_ObjectiveTrackerSubsystem::ResolveObjectiveSource()
{
	if (ObjectiveSource.IsValid())
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Provider = Locator->ResolveService(HUDTags::Service_ObjectiveSource))
		{
			if (Provider->GetClass()->ImplementsInterface(USeam_ObjectiveSource::StaticClass()))
			{
				ObjectiveSource = TWeakInterfacePtr<ISeam_ObjectiveSource>(*Provider);
			}
		}
	}
}

void UHUD_ObjectiveTrackerSubsystem::PinObjective(FGameplayTag ObjectiveId)
{
	if (ObjectiveId.IsValid())
	{
		PinnedIds.Add(ObjectiveId);
		Refresh();
	}
}

bool UHUD_ObjectiveTrackerSubsystem::UnpinObjective(FGameplayTag ObjectiveId)
{
	const bool bRemoved = PinnedIds.Remove(ObjectiveId) > 0;
	if (bRemoved)
	{
		Refresh();
	}
	return bRemoved;
}

void UHUD_ObjectiveTrackerSubsystem::HandleObjectiveBus(const FDP_Message& /*Message*/)
{
	Refresh();
}

bool UHUD_ObjectiveTrackerSubsystem::TickTracker(float /*DeltaTime*/)
{
	Refresh();
	return true;
}

UHUD_MarkerRegistrySubsystem* UHUD_ObjectiveTrackerSubsystem::ResolveRegistry() const
{
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UHUD_MarkerRegistrySubsystem* Reg =
				Locator->Resolve<UHUD_MarkerRegistrySubsystem>(HUDTags::Service_MarkerRegistry))
		{
			return Reg;
		}
	}
	return FDP_SubsystemStatics::GetWorldSubsystem<UHUD_MarkerRegistrySubsystem>(this);
}

void UHUD_ObjectiveTrackerSubsystem::Refresh()
{
	if (!ViewModel)
	{
		return;
	}

	ResolveObjectiveSource();

	TArray<FSeam_ObjectiveSnapshot> Snapshots;
	if (UObject* SourceObj = ObjectiveSource.GetObject())
	{
		if (SourceObj->GetClass()->ImplementsInterface(USeam_ObjectiveSource::StaticClass()))
		{
			ISeam_ObjectiveSource::Execute_GetTrackedObjectives(SourceObj, Snapshots);
		}
	}

	// Project into view rows, marking player-pinned entries.
	TArray<FHUD_ObjectiveView> Views;
	Views.Reserve(Snapshots.Num());
	for (const FSeam_ObjectiveSnapshot& Snap : Snapshots)
	{
		if (!Snap.IsValidObjective())
		{
			continue;
		}
		FHUD_ObjectiveView View;
		View.ObjectiveId = Snap.ObjectiveId;
		View.Title = Snap.Title;
		View.ProgressCurrent = Snap.ProgressCurrent;
		View.ProgressTarget = Snap.ProgressTarget;
		View.ProgressFraction = Snap.GetProgressFraction();
		View.StateTag = Snap.StateTag;
		View.bPinned = PinnedIds.Contains(Snap.ObjectiveId);
		View.bHasWorldLocation = Snap.bHasWorldLocation;
		View.WorldLocation = Snap.WorldLocation;
		Views.Add(MoveTemp(View));
	}

	// Pinned objectives sort to the top (stable within each group).
	Views.StableSort([](const FHUD_ObjectiveView& A, const FHUD_ObjectiveView& B)
	{
		return A.bPinned && !B.bPinned;
	});

	ViewModel->SetObjectives(Views);

	ReconcileWorldMarkers(Snapshots);
}

void UHUD_ObjectiveTrackerSubsystem::ReconcileWorldMarkers(const TArray<FSeam_ObjectiveSnapshot>& Snapshots)
{
	UHUD_MarkerRegistrySubsystem* Registry = ResolveRegistry();

	// Build the set of objective ids that currently want a world marker.
	TSet<FGameplayTag> Wanted;
	for (const FSeam_ObjectiveSnapshot& Snap : Snapshots)
	{
		if (Snap.IsValidObjective() && Snap.bHasWorldLocation)
		{
			Wanted.Add(Snap.ObjectiveId);

			// Update existing or create+register new.
			TObjectPtr<UHUD_ObjectiveTrackable>* Existing = WorldMarkers.Find(Snap.ObjectiveId);
			if (Existing && *Existing)
			{
				(*Existing)->SetSnapshot(Snap);
			}
			else
			{
				UHUD_ObjectiveTrackable* Bridge = NewObject<UHUD_ObjectiveTrackable>(this);
				Bridge->SetMarkerTag(WorldMarkerTag);
				Bridge->SetSnapshot(Snap);
				WorldMarkers.Add(Snap.ObjectiveId, Bridge);
				if (Registry)
				{
					Registry->RegisterTrackable(TScriptInterface<IHUD_Trackable>(Bridge));
				}
			}
		}
	}

	// Unregister + drop bridges whose objective no longer wants a world marker.
	for (auto It = WorldMarkers.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It->Key))
		{
			if (Registry && It->Value)
			{
				Registry->UnregisterTrackable(TScriptInterface<IHUD_Trackable>(It->Value));
			}
			It.RemoveCurrent();
		}
	}
}
