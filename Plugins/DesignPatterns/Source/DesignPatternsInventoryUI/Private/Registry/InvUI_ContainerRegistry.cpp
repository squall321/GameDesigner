// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Registry/InvUI_ContainerRegistry.h"
#include "InvUI_NativeTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "Engine/World.h"

void UInvUI_ContainerRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Entries.Reset();
	PublishToServiceLocator(/*bRegister=*/true);
	UE_LOG(LogDP, Verbose, TEXT("[InvUI] ContainerRegistry initialized for world '%s'."),
		*GetNameSafe(GetWorld()));
}

void UInvUI_ContainerRegistry::Deinitialize()
{
	PublishToServiceLocator(/*bRegister=*/false);
	Entries.Reset();
	Super::Deinitialize();
}

UInvUI_ContainerRegistry* UInvUI_ContainerRegistry::Get(const UObject* WorldContextObject)
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UInvUI_ContainerRegistry>(WorldContextObject);
}

void UInvUI_ContainerRegistry::PublishToServiceLocator(bool bRegister)
{
	// The locator is GameInstance-scoped; register as WeakObserved so a torn-down world cannot
	// leak this subsystem. Resolve through the world's game instance.
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (Locator == nullptr)
	{
		return;
	}

	if (bRegister)
	{
		Locator->RegisterService(InvUITags::Service_ContainerRegistry, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
	else if (Locator->ResolveService(InvUITags::Service_ContainerRegistry) == this)
	{
		// Only withdraw if WE are the published provider (another world may have replaced us).
		Locator->UnregisterService(InvUITags::Service_ContainerRegistry);
	}
}

bool UInvUI_ContainerRegistry::RegisterContainer(const TScriptInterface<IInvUI_ItemContainer>& Container)
{
	UObject* ContainerObj = Container.GetObject();
	if (ContainerObj == nullptr || Container.GetInterface() == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] RegisterContainer: null/invalid container, ignored."));
		return false;
	}

	const FInvUI_ContainerInstanceId Id =
		IInvUI_ItemContainer::Execute_GetContainerInstanceId(ContainerObj);
	if (!Id.IsValid())
	{
		UE_LOG(LogDP, Warning,
			TEXT("[InvUI] RegisterContainer: container '%s' reported an invalid instance id, ignored."),
			*GetNameSafe(ContainerObj));
		return false;
	}

	if (FEntry* Existing = Entries.Find(Id))
	{
		UObject* LiveObj = Existing->Container.IsValid() ? Existing->Container.GetObject() : nullptr;
		if (LiveObj != nullptr && LiveObj != ContainerObj)
		{
			UE_LOG(LogDP, Warning,
				TEXT("[InvUI] RegisterContainer: id %s already bound to a different live container '%s'; "
					"rejecting '%s'."),
				*Id.ToString(), *GetNameSafe(LiveObj), *GetNameSafe(ContainerObj));
			return false;
		}
		// Same object re-registering, or replacing a stale binding: fall through and rebind.
	}

	FEntry& Entry = Entries.FindOrAdd(Id);
	Entry.Container = TWeakInterfacePtr<IInvUI_ItemContainer>(Container.GetInterface());

	UE_LOG(LogDP, Verbose, TEXT("[InvUI] Registered container %s ('%s')."),
		*Id.ToString(), *GetNameSafe(ContainerObj));
	OnRegistryChanged.Broadcast(Id, /*bRegistered=*/true);
	return true;
}

bool UInvUI_ContainerRegistry::UnregisterContainer(const FInvUI_ContainerInstanceId& ContainerId)
{
	if (Entries.Remove(ContainerId) > 0)
	{
		UE_LOG(LogDP, Verbose, TEXT("[InvUI] Unregistered container %s."), *ContainerId.ToString());
		OnRegistryChanged.Broadcast(ContainerId, /*bRegistered=*/false);
		return true;
	}
	return false;
}

bool UInvUI_ContainerRegistry::UnregisterContainerObject(const TScriptInterface<IInvUI_ItemContainer>& Container)
{
	UObject* ContainerObj = Container.GetObject();
	if (ContainerObj == nullptr)
	{
		return false;
	}
	const FInvUI_ContainerInstanceId Id =
		IInvUI_ItemContainer::Execute_GetContainerInstanceId(ContainerObj);
	if (!Id.IsValid())
	{
		return false;
	}

	// Guard against removing a slot now owned by a *different* object with the same id.
	if (const FEntry* Existing = Entries.Find(Id))
	{
		UObject* LiveObj = Existing->Container.IsValid() ? Existing->Container.GetObject() : nullptr;
		if (LiveObj != nullptr && LiveObj != ContainerObj)
		{
			return false;
		}
	}
	return UnregisterContainer(Id);
}

TScriptInterface<IInvUI_ItemContainer> UInvUI_ContainerRegistry::ResolveContainer(
	const FInvUI_ContainerInstanceId& ContainerId) const
{
	const FEntry* Entry = Entries.Find(ContainerId);
	if (Entry == nullptr)
	{
		return TScriptInterface<IInvUI_ItemContainer>();
	}

	if (!Entry->Container.IsValid())
	{
		// Stale weak ref — the backend was GC'd without unregistering. Prune lazily.
		Entries.Remove(ContainerId);
		return TScriptInterface<IInvUI_ItemContainer>();
	}

	TScriptInterface<IInvUI_ItemContainer> Result;
	Result.SetObject(Entry->Container.GetObject());
	Result.SetInterface(Entry->Container.Get());
	return Result;
}

bool UInvUI_ContainerRegistry::IsRegistered(const FInvUI_ContainerInstanceId& ContainerId) const
{
	const FEntry* Entry = Entries.Find(ContainerId);
	return Entry != nullptr && Entry->Container.IsValid();
}

TArray<FInvUI_ContainerInstanceId> UInvUI_ContainerRegistry::GetRegisteredIds() const
{
	PruneStale();
	TArray<FInvUI_ContainerInstanceId> Ids;
	Ids.Reserve(Entries.Num());
	for (const TPair<FInvUI_ContainerInstanceId, FEntry>& Pair : Entries)
	{
		if (Pair.Value.Container.IsValid())
		{
			Ids.Add(Pair.Key);
		}
	}
	return Ids;
}

int32 UInvUI_ContainerRegistry::GetContainerCount() const
{
	return Entries.Num();
}

void UInvUI_ContainerRegistry::PruneStale() const
{
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		if (!It->Value.Container.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

FString UInvUI_ContainerRegistry::GetDPDebugString_Implementation() const
{
	PruneStale();
	return FString::Printf(TEXT("InvUI.ContainerRegistry: %d container(s) [authority=%s]"),
		Entries.Num(), HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}
