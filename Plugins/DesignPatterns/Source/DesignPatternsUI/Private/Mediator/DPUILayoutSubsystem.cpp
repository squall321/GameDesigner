// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mediator/DPUILayoutSubsystem.h"
#include "Mediator/DPLayerStack.h"
#include "Core/DPLog.h"

void UDP_UILayoutSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Log, TEXT("[UI] Layout subsystem initialized for local player %s."),
		*GetName());
}

void UDP_UILayoutSubsystem::Deinitialize()
{
	ClearAllLayers();
	Layers.Empty();
	Super::Deinitialize();
}

UDP_LayerStack* UDP_UILayoutSubsystem::GetOrCreateLayer(FGameplayTag LayerTag)
{
	if (!LayerTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[UI] GetOrCreateLayer called with an invalid layer tag."));
		return nullptr;
	}

	if (TObjectPtr<UDP_LayerStack>* Existing = Layers.Find(LayerTag))
	{
		return *Existing;
	}

	// Outer is this subsystem so the stack shares the local-player lifetime.
	UDP_LayerStack* NewStack = NewObject<UDP_LayerStack>(this);
	NewStack->InitLayer(LayerTag);
	Layers.Add(LayerTag, NewStack);

	UE_LOG(LogDP, Verbose, TEXT("[UI] Created layer stack '%s'."), *LayerTag.ToString());
	return NewStack;
}

UDP_LayerStack* UDP_UILayoutSubsystem::FindLayer(FGameplayTag LayerTag) const
{
	const TObjectPtr<UDP_LayerStack>* Existing = Layers.Find(LayerTag);
	return Existing ? *Existing : nullptr;
}

void UDP_UILayoutSubsystem::GetActiveLayers(TArray<FGameplayTag>& OutLayers) const
{
	OutLayers.Reset();
	Layers.GetKeys(OutLayers);
}

void UDP_UILayoutSubsystem::ClearAllLayers()
{
	for (TPair<FGameplayTag, TObjectPtr<UDP_LayerStack>>& Pair : Layers)
	{
		if (Pair.Value)
		{
			Pair.Value->Clear();
		}
	}
}

void UDP_UILayoutSubsystem::DumpTo(TArray<FString>& OutLines) const
{
	OutLines.Add(FString::Printf(TEXT("LocalPlayer '%s' — %d layer(s):"),
		*GetName(), Layers.Num()));

	for (const TPair<FGameplayTag, TObjectPtr<UDP_LayerStack>>& Pair : Layers)
	{
		if (Pair.Value)
		{
			Pair.Value->DumpTo(OutLines);
		}
	}
}
