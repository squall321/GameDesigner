// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Minimap/HUD_MarkerRegistrySubsystem.h"

#include "Seam/HUD_Trackable.h"
#include "HUD_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Engine/World.h"

void UHUD_MarkerRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Publish ourselves as a weak-observed service so the cosmetic minimap ViewModel can resolve the
	// registry by stable tag without depending on this concrete world-subsystem type.
	PublishToLocator(/*bRegister=*/true);

	UE_LOG(LogDP, Log, TEXT("HUD_MarkerRegistrySubsystem initialized (authority=%d)."),
		HasWorldAuthority() ? 1 : 0);
}

void UHUD_MarkerRegistrySubsystem::Deinitialize()
{
	PublishToLocator(/*bRegister=*/false);
	Trackables.Reset();

	Super::Deinitialize();
}

void UHUD_MarkerRegistrySubsystem::PublishToLocator(bool bRegister)
{
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}

	if (bRegister)
	{
		// WeakObserved: the locator must NOT keep this world subsystem alive across level travel.
		Locator->RegisterService(HUDTags::Service_MarkerRegistry, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
	else if (Locator->Resolve<UHUD_MarkerRegistrySubsystem>(HUDTags::Service_MarkerRegistry) == this)
	{
		// Only withdraw if we are still the bound provider (a newer world may have replaced us).
		Locator->UnregisterService(HUDTags::Service_MarkerRegistry);
	}
}

void UHUD_MarkerRegistrySubsystem::RegisterTrackable(TScriptInterface<IHUD_Trackable> Trackable)
{
	UObject* Obj = Trackable.GetObject();
	if (!Obj || !Trackable.GetInterface())
	{
		UE_LOG(LogDP, Warning, TEXT("HUD_MarkerRegistry: rejected null/non-trackable registration."));
		return;
	}

	// Construct the weak interface ref from the interface pointer (non-owning; null-checked on read).
	const TWeakInterfacePtr<IHUD_Trackable> Weak(*Trackable.GetInterface());

	// Idempotent: skip if already present (and opportunistically drop any matching-stale slots).
	for (const TWeakInterfacePtr<IHUD_Trackable>& Existing : Trackables)
	{
		if (Existing.GetObject() == Obj)
		{
			return;
		}
	}

	Trackables.Add(Weak);
	OnMarkerSetChanged.Broadcast();

	UE_LOG(LogDP, Verbose, TEXT("HUD_MarkerRegistry: registered '%s' (count=%d)."),
		*GetNameSafe(Obj), Trackables.Num());
}

void UHUD_MarkerRegistrySubsystem::UnregisterTrackable(TScriptInterface<IHUD_Trackable> Trackable)
{
	const UObject* Obj = Trackable.GetObject();
	if (!Obj)
	{
		return;
	}

	const int32 Removed = Trackables.RemoveAll(
		[Obj](const TWeakInterfacePtr<IHUD_Trackable>& Entry)
		{
			// Remove the match AND any already-stale entries to keep the set tidy.
			return !Entry.IsValid() || Entry.GetObject() == Obj;
		});

	if (Removed > 0)
	{
		OnMarkerSetChanged.Broadcast();
		UE_LOG(LogDP, Verbose, TEXT("HUD_MarkerRegistry: unregistered '%s' (count=%d)."),
			*GetNameSafe(Obj), Trackables.Num());
	}
}

void UHUD_MarkerRegistrySubsystem::GetLiveTrackables(
	TArray<TScriptInterface<IHUD_Trackable>>& OutTrackables) const
{
	PruneStale();

	OutTrackables.Reset(Trackables.Num());
	for (const TWeakInterfacePtr<IHUD_Trackable>& Weak : Trackables)
	{
		if (UObject* Obj = Weak.GetObject())
		{
			// Re-wrap as a strong TScriptInterface for the duration of this refresh; the registry keeps
			// only weak refs, so this temporary strong ref does not extend lifetime beyond the array.
			TScriptInterface<IHUD_Trackable> Script;
			Script.SetObject(Obj);
			Script.SetInterface(Weak.Get());
			OutTrackables.Add(MoveTemp(Script));
		}
	}
}

int32 UHUD_MarkerRegistrySubsystem::GetTrackableCount() const
{
	PruneStale();
	return Trackables.Num();
}

void UHUD_MarkerRegistrySubsystem::PruneStale() const
{
	Trackables.RemoveAll(
		[](const TWeakInterfacePtr<IHUD_Trackable>& Entry)
		{
			return !Entry.IsValid();
		});
}

FString UHUD_MarkerRegistrySubsystem::GetDPDebugString_Implementation() const
{
	PruneStale();
	return FString::Printf(TEXT("HUD MarkerRegistry: %d trackable(s)"), Trackables.Num());
}
