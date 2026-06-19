// Copyright DesignPatterns plugin. All Rights Reserved.

#include "PubSub/DPUIMessageListener.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

UDP_UIMessageListener* UDP_UIMessageListener::CreateUIMessageListener(UObject* OwnerForLifetime)
{
	if (!OwnerForLifetime)
	{
		UE_LOG(LogDP, Warning, TEXT("[UI] CreateUIMessageListener called with a null owner."));
		return nullptr;
	}
	// Outer is the owner so the adapter shares the owner's GC lifetime.
	return NewObject<UDP_UIMessageListener>(OwnerForLifetime);
}

UDP_MessageBusSubsystem* UDP_UIMessageListener::ResolveBus() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
}

void UDP_UIMessageListener::Begin(FGameplayTag Channel, EDP_MessageMatch Match)
{
	if (!Channel.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[UI] MessageListener Begin called with an invalid channel."));
		return;
	}

	// Re-subscribing: drop any prior subscription first.
	if (Handle.IsValid())
	{
		Stop();
	}

	UDP_MessageBusSubsystem* Bus = ResolveBus();
	if (!Bus)
	{
		UE_LOG(LogDP, Verbose, TEXT("[UI] MessageListener Begin: bus unavailable (design-time?); '%s' not subscribed."),
			*Channel.ToString());
		return;
	}

	BoundBus = Bus;
	SubscribedChannel = Channel;

	// Forward native bus callbacks to the Blueprint-assignable multicast. WeakLambda guards lifetime.
	TWeakObjectPtr<UDP_UIMessageListener> WeakThis(this);
	Handle = Bus->ListenNative(Channel,
		[WeakThis](const FDP_Message& Message)
		{
			if (UDP_UIMessageListener* Self = WeakThis.Get())
			{
				Self->OnMessageReceived.Broadcast(Message.Channel, Message.Payload, Message.Instigator.Get());
			}
		},
		this,
		Match);

	UE_LOG(LogDP, Verbose, TEXT("[UI] MessageListener subscribed to '%s'."), *Channel.ToString());
}

void UDP_UIMessageListener::Stop()
{
	if (!Handle.IsValid())
	{
		return;
	}

	if (UDP_MessageBusSubsystem* Bus = BoundBus.Get())
	{
		Bus->StopListening(Handle);
	}

	Handle = FDP_ListenerHandle();
	SubscribedChannel = FGameplayTag();
	BoundBus.Reset();
}

void UDP_UIMessageListener::BeginDestroy()
{
	// Backstop: deterministic unsubscribe if the owner is collected without an explicit Stop().
	Stop();
	Super::BeginDestroy();
}
