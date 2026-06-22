// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Subscriptions/WorldHub_SubscriptionSubsystem.h"
#include "Hub/WorldHub_StateHubSubsystem.h"

#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

void UWorldHub_SubscriptionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResolveHub();
	UE_LOG(LogDP, Log, TEXT("[WorldHub] Subscription subsystem initialized."));
}

void UWorldHub_SubscriptionSubsystem::Deinitialize()
{
	if (UWorldHub_StateHubSubsystem* H = Hub.Get())
	{
		H->OnValueChanged.RemoveAll(this);
	}
	Subscriptions.Reset();
	Hub.Reset();
	Super::Deinitialize();
}

UWorldHub_StateHubSubsystem* UWorldHub_SubscriptionSubsystem::ResolveHub()
{
	if (UWorldHub_StateHubSubsystem* Cached = Hub.Get())
	{
		return Cached;
	}
	UWorldHub_StateHubSubsystem* Resolved =
		FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
	if (Resolved)
	{
		Hub = Resolved;
		Resolved->OnValueChanged.AddUniqueDynamic(this, &UWorldHub_SubscriptionSubsystem::OnHubValueChanged);
	}
	return Resolved;
}

FWorldHub_SubscriptionHandle UWorldHub_SubscriptionSubsystem::Subscribe(const FWorldHub_SubscriptionFilter& Filter, const FWorldHub_OnScopedChange& Callback)
{
	if (!Callback.IsBound())
	{
		return FWorldHub_SubscriptionHandle();
	}
	// Make sure we are bound to the hub even if Initialize ran before the hub existed.
	ResolveHub();

	const int64 Id = NextHandleId++;
	FWorldHub_Subscription& Sub = Subscriptions.Add(Id);
	Sub.Filter = Filter;
	Sub.Callback = Callback;
	return FWorldHub_SubscriptionHandle(Id);
}

bool UWorldHub_SubscriptionSubsystem::Unsubscribe(FWorldHub_SubscriptionHandle Handle)
{
	return Handle.IsValid() && Subscriptions.Remove(Handle.Id) > 0;
}

int32 UWorldHub_SubscriptionSubsystem::UnsubscribeAllForObject(UObject* Object)
{
	if (!Object)
	{
		return 0;
	}
	int32 Removed = 0;
	for (auto It = Subscriptions.CreateIterator(); It; ++It)
	{
		if (It.Value().Callback.GetUObject() == Object)
		{
			It.RemoveCurrent();
			++Removed;
		}
	}
	return Removed;
}

void UWorldHub_SubscriptionSubsystem::OnHubValueChanged(FWorldHub_Scope Scope, FGameplayTag Key, FSeam_NetValue NewValue)
{
	// Iterate a copy of the handles so a callback may safely (un)subscribe during dispatch.
	TArray<int64> Handles;
	Subscriptions.GenerateKeyArray(Handles);

	for (int64 Id : Handles)
	{
		FWorldHub_Subscription* Sub = Subscriptions.Find(Id);
		if (!Sub)
		{
			continue; // Removed mid-dispatch.
		}
		if (!Sub->Callback.IsBound())
		{
			// Bound object went away — drop the dead subscription.
			Subscriptions.Remove(Id);
			continue;
		}
		if (Sub->Filter.Matches(Scope, Key))
		{
			Sub->Callback.ExecuteIfBound(Scope, Key, NewValue);
		}
	}
}

FString UWorldHub_SubscriptionSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("WorldHub Subscriptions=%d Hub=%s"),
		Subscriptions.Num(), Hub.IsValid() ? TEXT("bound") : TEXT("none"));
}
