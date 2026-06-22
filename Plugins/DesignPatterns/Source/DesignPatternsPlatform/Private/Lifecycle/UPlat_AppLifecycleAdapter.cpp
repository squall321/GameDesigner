// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Lifecycle/UPlat_AppLifecycleAdapter.h"
#include "Lifecycle/UPlat_AppLifecycleSubsystem.h"
#include "Core/DPLog.h"

void UPlat_AppLifecycleAdapter::BindToSubsystem(UPlat_AppLifecycleSubsystem* InSubsystem)
{
	SubsystemWeak = InSubsystem;
	if (InSubsystem)
	{
		InSubsystem->OnAppSuspended.AddDynamic(this, &UPlat_AppLifecycleAdapter::HandleSuspended);
		InSubsystem->OnAppResumed.AddDynamic(this, &UPlat_AppLifecycleAdapter::HandleResumed);
	}
}

void UPlat_AppLifecycleAdapter::Shutdown()
{
	if (UPlat_AppLifecycleSubsystem* Subsystem = SubsystemWeak.Get())
	{
		Subsystem->OnAppSuspended.RemoveDynamic(this, &UPlat_AppLifecycleAdapter::HandleSuspended);
		Subsystem->OnAppResumed.RemoveDynamic(this, &UPlat_AppLifecycleAdapter::HandleResumed);
	}
	SubsystemWeak.Reset();
	Listeners.Empty();
}

void UPlat_AppLifecycleAdapter::PruneListeners()
{
	Listeners.RemoveAll([](const TWeakObjectPtr<UObject>& W) { return !W.IsValid(); });
}

void UPlat_AppLifecycleAdapter::HandleSuspended()
{
	PruneListeners();
	for (const TWeakObjectPtr<UObject>& W : Listeners)
	{
		if (UObject* Obj = W.Get())
		{
			if (Obj->GetClass()->ImplementsInterface(USeam_LifecycleListener::StaticClass()))
			{
				ISeam_LifecycleListener::Execute_OnAppSuspended(Obj);
			}
		}
	}
}

void UPlat_AppLifecycleAdapter::HandleResumed()
{
	PruneListeners();
	for (const TWeakObjectPtr<UObject>& W : Listeners)
	{
		if (UObject* Obj = W.Get())
		{
			if (Obj->GetClass()->ImplementsInterface(USeam_LifecycleListener::StaticClass()))
			{
				ISeam_LifecycleListener::Execute_OnAppResumed(Obj);
			}
		}
	}
}

bool UPlat_AppLifecycleAdapter::IsSuspended_Implementation() const
{
	const UPlat_AppLifecycleSubsystem* Subsystem = SubsystemWeak.Get();
	return Subsystem ? Subsystem->IsSuspended() : false;
}

void UPlat_AppLifecycleAdapter::RegisterLifecycleListener_Implementation(const TScriptInterface<ISeam_LifecycleListener>& Listener)
{
	UObject* Obj = Listener.GetObject();
	if (!Obj)
	{
		return;
	}
	PruneListeners();
	// Dedupe.
	for (const TWeakObjectPtr<UObject>& W : Listeners)
	{
		if (W.Get() == Obj)
		{
			return;
		}
	}
	Listeners.Add(Obj);
}

void UPlat_AppLifecycleAdapter::UnregisterLifecycleListener_Implementation(const TScriptInterface<ISeam_LifecycleListener>& Listener)
{
	UObject* Obj = Listener.GetObject();
	if (!Obj)
	{
		return;
	}
	Listeners.RemoveAll([Obj](const TWeakObjectPtr<UObject>& W) { return W.Get() == Obj; });
}
