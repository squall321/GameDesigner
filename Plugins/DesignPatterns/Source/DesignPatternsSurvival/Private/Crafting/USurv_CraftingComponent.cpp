// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crafting/USurv_CraftingComponent.h"
#include "Crafting/USurv_Recipe.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

USurv_CraftingComponent::USurv_CraftingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USurv_CraftingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CraftTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool USurv_CraftingComponent::HasAuthorityToMutate() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

float USurv_CraftingComponent::GetWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

USurv_Recipe* USurv_CraftingComponent::ResolveRecipe(const FGameplayTag& RecipeTag) const
{
	if (!RecipeTag.IsValid())
	{
		return nullptr;
	}
	// Recipes are tag-identified data assets resolved through the core registry (no path coupling).
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Registry->Find<USurv_Recipe>(RecipeTag);
	}
	return nullptr;
}

bool USurv_CraftingComponent::HasRequiredStation(const USurv_Recipe* Recipe) const
{
	if (!Recipe || !Recipe->RequiredStationTag.IsValid())
	{
		return true; // hand-craftable
	}
	// Hierarchy-aware: a provided "Surv.Station.Forge" satisfies a required "Surv.Station".
	return AvailableStations.HasTag(Recipe->RequiredStationTag);
}

bool USurv_CraftingComponent::CanAfford(const USurv_Recipe* Recipe) const
{
	if (!Recipe)
	{
		return false;
	}
	if (!ResourceStore)
	{
		return Recipe->Inputs.Num() == 0;
	}
	for (const FSurv_ResourceStack& In : Recipe->Inputs)
	{
		if (!ResourceStore->HasAtLeast(In.ItemTag, In.Count))
		{
			return false;
		}
	}
	return true;
}

bool USurv_CraftingComponent::StartCraft(FGameplayTag RecipeTag)
{
	if (!HasAuthorityToMutate())
	{
		return false;
	}
	const USurv_Recipe* Recipe = ResolveRecipe(RecipeTag);
	if (!Recipe)
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] StartCraft: no recipe for tag %s"), *RecipeTag.ToString());
		return false;
	}

	FSurv_CraftJob Job;
	Job.RecipeTag = RecipeTag;
	Job.bStarted = false;
	Queue.Add(Job);

	OnCraftQueued.Broadcast(RecipeTag);

	// If this is the only job, attempt to start it immediately.
	if (Queue.Num() == 1)
	{
		TryStartFrontJob();
	}
	return true;
}

void USurv_CraftingComponent::TryStartFrontJob()
{
	if (!HasAuthorityToMutate() || Queue.Num() == 0)
	{
		return;
	}
	FSurv_CraftJob& Front = Queue[0];
	if (Front.bStarted)
	{
		return;
	}

	const USurv_Recipe* Recipe = ResolveRecipe(Front.RecipeTag);
	if (!Recipe || !HasRequiredStation(Recipe) || !CanAfford(Recipe))
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] Craft %s cannot start (station/inputs); dropping job"),
			*Front.RecipeTag.ToString());
		const FGameplayTag Dropped = Front.RecipeTag;
		Queue.RemoveAt(0);
		OnCraftCancelled.Broadcast(Dropped, /*bRefunded*/ false);
		TryStartFrontJob(); // try the next one
		return;
	}

	// Consume inputs up front so they cannot be double-spent while crafting.
	if (ResourceStore)
	{
		for (const FSurv_ResourceStack& In : Recipe->Inputs)
		{
			ResourceStore->RemoveResource(In.ItemTag, In.Count);
		}
	}

	Front.bStarted = true;
	Front.CompleteWorldTime = GetWorldTimeSeconds() + FMath::Max(0.f, Recipe->CraftTimeSeconds);

	if (Recipe->CraftTimeSeconds <= 0.f)
	{
		HandleCraftComplete();
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CraftTimerHandle, this, &USurv_CraftingComponent::HandleCraftComplete,
			Recipe->CraftTimeSeconds, /*bLoop*/ false);
	}
}

void USurv_CraftingComponent::HandleCraftComplete()
{
	if (!HasAuthorityToMutate() || Queue.Num() == 0)
	{
		return;
	}
	const FGameplayTag RecipeTag = Queue[0].RecipeTag;
	const USurv_Recipe* Recipe = ResolveRecipe(RecipeTag);

	if (Recipe && ResourceStore)
	{
		for (const FSurv_ResourceStack& Out : Recipe->Outputs)
		{
			ResourceStore->AddResource(Out.ItemTag, Out.Count);
		}
	}

	Queue.RemoveAt(0);
	UE_LOG(LogDP, Verbose, TEXT("[Survival] Craft complete: %s"), *RecipeTag.ToString());
	OnCraftCompleted.Broadcast(RecipeTag);

	// Advance to the next queued job.
	TryStartFrontJob();
}

void USurv_CraftingComponent::RefundInputs(const FGameplayTag& RecipeTag)
{
	const USurv_Recipe* Recipe = ResolveRecipe(RecipeTag);
	if (Recipe && ResourceStore)
	{
		for (const FSurv_ResourceStack& In : Recipe->Inputs)
		{
			ResourceStore->AddResource(In.ItemTag, In.Count);
		}
	}
}

bool USurv_CraftingComponent::CancelCraft(int32 QueueIndex)
{
	if (!HasAuthorityToMutate())
	{
		return false;
	}
	if (!Queue.IsValidIndex(QueueIndex))
	{
		return false;
	}

	const FSurv_CraftJob Job = Queue[QueueIndex];
	const bool bWasActive = (QueueIndex == 0) && Job.bStarted;

	if (bWasActive)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(CraftTimerHandle);
		}
		RefundInputs(Job.RecipeTag);
	}

	Queue.RemoveAt(QueueIndex);
	OnCraftCancelled.Broadcast(Job.RecipeTag, /*bRefunded*/ bWasActive);

	// If we cancelled the active job, kick the next one.
	if (QueueIndex == 0)
	{
		TryStartFrontJob();
	}
	return true;
}

void USurv_CraftingComponent::CancelAll()
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	// Cancel from the front repeatedly so refund/restart logic stays correct.
	while (Queue.Num() > 0)
	{
		CancelCraft(0);
	}
}

void USurv_CraftingComponent::SetAvailableStations(const FGameplayTagContainer& Stations)
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	AvailableStations = Stations;
	// A newly available station may unblock a queued head job.
	TryStartFrontJob();
}

float USurv_CraftingComponent::GetActiveProgress() const
{
	if (Queue.Num() == 0 || !Queue[0].bStarted)
	{
		return 0.f;
	}
	const USurv_Recipe* Recipe = ResolveRecipe(Queue[0].RecipeTag);
	if (!Recipe || Recipe->CraftTimeSeconds <= 0.f)
	{
		return 1.f;
	}
	const float Remaining = Queue[0].CompleteWorldTime - GetWorldTimeSeconds();
	return FMath::Clamp(1.f - (Remaining / Recipe->CraftTimeSeconds), 0.f, 1.f);
}
