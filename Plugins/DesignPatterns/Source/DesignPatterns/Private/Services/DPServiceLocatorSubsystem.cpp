// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPLog.h"
#include "Stats/Stats.h"

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Services Registered"), STAT_DPServiceCount, STATGROUP_DesignPatterns);
DECLARE_CYCLE_STAT(TEXT("Service Resolve"), STAT_DPServiceResolve, STATGROUP_DesignPatterns);

void UDP_ServiceLocatorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDPService, Verbose, TEXT("ServiceLocator initialized."));
}

void UDP_ServiceLocatorSubsystem::Deinitialize()
{
	// Notify any remaining listeners that every binding is going away, then drop refs so
	// StrongOwned providers become eligible for GC with the subsystem.
	for (const TPair<FGameplayTag, FDP_ServiceEntry>& Pair : Services)
	{
		OnServiceInvalidated.Broadcast(Pair.Key);
	}
	Services.Reset();
	SET_DWORD_STAT(STAT_DPServiceCount, 0);
	Super::Deinitialize();
}

bool UDP_ServiceLocatorSubsystem::RegisterService(FGameplayTag Key, UObject* Provider, EDP_ServiceLifetime Lifetime, bool bAllowOverride)
{
	if (!Key.IsValid())
	{
		UE_LOG(LogDPService, Warning, TEXT("RegisterService rejected: invalid Key."));
		return false;
	}
	if (!IsValid(Provider))
	{
		UE_LOG(LogDPService, Warning, TEXT("RegisterService(%s) rejected: null/invalid Provider."), *Key.ToString());
		return false;
	}

	if (FDP_ServiceEntry* Existing = Services.Find(Key))
	{
		// A stale weak slot is always safely replaceable; a live one needs explicit override.
		if (Existing->IsLive())
		{
			if (!bAllowOverride)
			{
				UE_LOG(LogDPService, Warning,
					TEXT("RegisterService(%s) rejected: already bound to '%s' (pass bAllowOverride to replace)."),
					*Key.ToString(), *GetNameSafe(Existing->GetProvider()));
				return false;
			}
			UE_LOG(LogDPService, Log, TEXT("RegisterService(%s): overriding existing provider '%s'."),
				*Key.ToString(), *GetNameSafe(Existing->GetProvider()));
		}
		// Replacing (override or stale) invalidates the previous binding first.
		OnServiceInvalidated.Broadcast(Key);
	}

	FDP_ServiceEntry Entry;
	Entry.Lifetime = Lifetime;
	if (Lifetime == EDP_ServiceLifetime::StrongOwned)
	{
		Entry.StrongProvider = Provider;
	}
	else
	{
		Entry.WeakProvider = Provider;
	}
	Services.Add(Key, MoveTemp(Entry));
	SET_DWORD_STAT(STAT_DPServiceCount, Services.Num());

	if (bEnableVerboseLogging)
	{
		UE_LOG(LogDPService, Verbose, TEXT("Registered service %s -> %s (%s)."),
			*Key.ToString(), *Provider->GetName(),
			Lifetime == EDP_ServiceLifetime::StrongOwned ? TEXT("StrongOwned") : TEXT("WeakObserved"));
	}

	OnServiceRegistered.Broadcast(Key, Provider);
	return true;
}

UObject* UDP_ServiceLocatorSubsystem::ResolveService(FGameplayTag Key) const
{
	SCOPE_CYCLE_COUNTER(STAT_DPServiceResolve);

	if (!Key.IsValid())
	{
		return nullptr;
	}

	const FDP_ServiceEntry* Entry = Services.Find(Key);
	if (!Entry)
	{
		return nullptr;
	}

	if (Entry->Lifetime == EDP_ServiceLifetime::StrongOwned)
	{
		return Entry->StrongProvider;
	}

	// WeakObserved: a registered-but-dead provider means the registrant leaked it (failed to
	// keep it alive) or forgot to Unregister. Surface that in non-shipping, then lazily clean up.
	if (!Entry->WeakProvider.IsValid())
	{
		if (Services.Contains(Key))
		{
			ensureMsgf(false,
				TEXT("ServiceLocator: resolved a dangling WeakObserved provider for key '%s'. "
					 "The provider was GC'd without UnregisterService."),
				*Key.ToString());
		}
		// Invalidate the dead slot so subsequent resolves return cleanly.
		const_cast<UDP_ServiceLocatorSubsystem*>(this)->UnregisterService(Key);
		return nullptr;
	}

	return Entry->WeakProvider.Get();
}

bool UDP_ServiceLocatorSubsystem::IsRegistered(FGameplayTag Key) const
{
	const FDP_ServiceEntry* Entry = Services.Find(Key);
	return Entry != nullptr && Entry->IsLive();
}

bool UDP_ServiceLocatorSubsystem::UnregisterService(FGameplayTag Key)
{
	if (Services.Remove(Key) > 0)
	{
		SET_DWORD_STAT(STAT_DPServiceCount, Services.Num());
		if (bEnableVerboseLogging)
		{
			UE_LOG(LogDPService, Verbose, TEXT("Unregistered service %s."), *Key.ToString());
		}
		OnServiceInvalidated.Broadcast(Key);
		return true;
	}
	return false;
}

int32 UDP_ServiceLocatorSubsystem::GetServiceCount() const
{
	return Services.Num();
}

void UDP_ServiceLocatorSubsystem::PruneStale() const
{
	// Gather dead weak slots first; mutate after iterating to keep the map stable.
	TArray<FGameplayTag> Dead;
	for (const TPair<FGameplayTag, FDP_ServiceEntry>& Pair : Services)
	{
		if (Pair.Value.Lifetime == EDP_ServiceLifetime::WeakObserved && !Pair.Value.WeakProvider.IsValid())
		{
			Dead.Add(Pair.Key);
		}
	}

	if (Dead.Num() > 0)
	{
		UDP_ServiceLocatorSubsystem* Mutable = const_cast<UDP_ServiceLocatorSubsystem*>(this);
		for (const FGameplayTag& Key : Dead)
		{
			Mutable->Services.Remove(Key);
			Mutable->OnServiceInvalidated.Broadcast(Key);
		}
		SET_DWORD_STAT(STAT_DPServiceCount, Services.Num());
		UE_LOG(LogDPService, Verbose, TEXT("Pruned %d stale (GC'd) service binding(s)."), Dead.Num());
	}
}

FString UDP_ServiceLocatorSubsystem::GetDPDebugString_Implementation() const
{
	int32 LiveCount = 0;
	for (const TPair<FGameplayTag, FDP_ServiceEntry>& Pair : Services)
	{
		if (Pair.Value.IsLive())
		{
			++LiveCount;
		}
	}
	return FString::Printf(TEXT("ServiceLocator: %d/%d live bindings"), LiveCount, Services.Num());
}

void UDP_ServiceLocatorSubsystem::DumpServices() const
{
	// Clean up dead weak slots so the dump reflects reality.
	PruneStale();

	UE_LOG(LogDPService, Log, TEXT("=== Service Locator: %d registered ==="), Services.Num());
	for (const TPair<FGameplayTag, FDP_ServiceEntry>& Pair : Services)
	{
		const FDP_ServiceEntry& Entry = Pair.Value;
		const UObject* Provider = Entry.GetProvider();
		UE_LOG(LogDPService, Log, TEXT("  %s : %s -> %s%s"),
			*Pair.Key.ToString(),
			Entry.Lifetime == EDP_ServiceLifetime::StrongOwned ? TEXT("StrongOwned") : TEXT("WeakObserved"),
			Provider ? *Provider->GetName() : TEXT("<stale>"),
			Entry.IsLive() ? TEXT("") : TEXT("  [INVALID]"));
	}
}
