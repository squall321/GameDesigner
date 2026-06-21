// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crafting/USurv_KnowledgeComponent.h"
#include "Tech/USurv_TechNode.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "DesignPatternsSurvivalTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

USurv_KnowledgeComponent::USurv_KnowledgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USurv_KnowledgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ResearchTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void USurv_KnowledgeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USurv_KnowledgeComponent, KnownTechTags);
	DOREPLIFETIME(USurv_KnowledgeComponent, DiscoveredRecipeTags);
	DOREPLIFETIME(USurv_KnowledgeComponent, ActiveResearchTechTag);
	DOREPLIFETIME(USurv_KnowledgeComponent, ResearchCompleteWorldTime);
	DOREPLIFETIME(USurv_KnowledgeComponent, ResearchStartWorldTime);
}

bool USurv_KnowledgeComponent::HasAuthorityToMutate() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

float USurv_KnowledgeComponent::GetWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

// ---- Queries ----

bool USurv_KnowledgeComponent::HasTech(FGameplayTag TechTag) const
{
	// Hierarchy-aware: a known "Surv.Tech.Smithing.Advanced" satisfies a query for "Surv.Tech.Smithing".
	return TechTag.IsValid() && KnownTechTags.HasTag(TechTag);
}

bool USurv_KnowledgeComponent::HasAllTech(const FGameplayTagContainer& Required) const
{
	return Required.IsEmpty() || KnownTechTags.HasAll(Required);
}

bool USurv_KnowledgeComponent::IsRecipeUnlocked(FGameplayTag RecipeTag) const
{
	return RecipeTag.IsValid() && DiscoveredRecipeTags.HasTagExact(RecipeTag);
}

float USurv_KnowledgeComponent::GetActiveResearchProgress() const
{
	if (!ActiveResearchTechTag.IsValid())
	{
		return 0.f;
	}
	const float Total = ResearchCompleteWorldTime - ResearchStartWorldTime;
	if (Total <= 0.f)
	{
		return 1.f;
	}
	const float Elapsed = GetWorldTimeSeconds() - ResearchStartWorldTime;
	return FMath::Clamp(Elapsed / Total, 0.f, 1.f);
}

// ---- Authority mutators ----

bool USurv_KnowledgeComponent::GrantTech(FGameplayTag TechTag)
{
	if (!HasAuthorityToMutate() || !TechTag.IsValid())
	{
		return false;
	}
	if (KnownTechTags.HasTagExact(TechTag))
	{
		return false;
	}
	KnownTechTags.AddTag(TechTag);
	OnTechGranted.Broadcast(TechTag);
	LastBroadcastTech = KnownTechTags;
	NotifyKnowledgeChanged();
	UE_LOG(LogDP, Verbose, TEXT("[Survival] Tech granted: %s"), *TechTag.ToString());
	return true;
}

bool USurv_KnowledgeComponent::DiscoverRecipe(FGameplayTag RecipeTag)
{
	if (!HasAuthorityToMutate() || !RecipeTag.IsValid())
	{
		return false;
	}
	if (DiscoveredRecipeTags.HasTagExact(RecipeTag))
	{
		return false;
	}
	DiscoveredRecipeTags.AddTag(RecipeTag);
	OnRecipeDiscovered.Broadcast(RecipeTag);
	LastBroadcastRecipes = DiscoveredRecipeTags;
	NotifyKnowledgeChanged();
	UE_LOG(LogDP, Verbose, TEXT("[Survival] Recipe discovered: %s"), *RecipeTag.ToString());
	return true;
}

USurv_TechNode* USurv_KnowledgeComponent::ResolveTechNode(const FGameplayTag& TechNodeTag) const
{
	if (!TechNodeTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Registry->Find<USurv_TechNode>(TechNodeTag);
	}
	return nullptr;
}

bool USurv_KnowledgeComponent::BeginResearch(FGameplayTag TechNodeTag)
{
	if (!HasAuthorityToMutate())
	{
		return false;
	}
	if (ActiveResearchTechTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] BeginResearch: already researching %s"),
			*ActiveResearchTechTag.ToString());
		return false;
	}

	const USurv_TechNode* Node = ResolveTechNode(TechNodeTag);
	if (!Node)
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] BeginResearch: no tech node for %s"), *TechNodeTag.ToString());
		return false;
	}

	const FGameplayTag Granted = Node->GetGrantedTechTag();
	if (KnownTechTags.HasTagExact(Granted))
	{
		return false; // already known
	}
	if (!HasAllTech(Node->PrerequisiteTechTags))
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] BeginResearch: prerequisites unmet for %s"), *TechNodeTag.ToString());
		return false;
	}

	// Validate affordability, then consume cost atomically (require everything, then remove).
	if (ResourceStore)
	{
		for (const FSurv_ResourceStack& Cost : Node->ResearchCost)
		{
			if (!ResourceStore->HasAtLeast(Cost.ItemTag, Cost.Count))
			{
				UE_LOG(LogDP, Warning, TEXT("[Survival] BeginResearch: cannot afford %s"), *TechNodeTag.ToString());
				return false;
			}
		}
		for (const FSurv_ResourceStack& Cost : Node->ResearchCost)
		{
			ResourceStore->RemoveResource(Cost.ItemTag, Cost.Count);
		}
	}

	const float Now = GetWorldTimeSeconds();
	const float Duration = FMath::Max(0, Node->ResearchTimeSeconds);
	ActiveResearchTechTag = Granted;
	ResearchStartWorldTime = Now;
	ResearchCompleteWorldTime = Now + Duration;

	if (Duration <= 0.f)
	{
		HandleResearchComplete();
		return true;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ResearchTimerHandle, this, &USurv_KnowledgeComponent::HandleResearchComplete,
			Duration, /*bLoop*/ false);
	}
	OnResearchProgress.Broadcast(Granted, 0.f);
	return true;
}

void USurv_KnowledgeComponent::HandleResearchComplete()
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	const FGameplayTag Granted = ActiveResearchTechTag;
	ActiveResearchTechTag = FGameplayTag();
	ResearchStartWorldTime = 0.f;
	ResearchCompleteWorldTime = 0.f;

	if (Granted.IsValid())
	{
		GrantTech(Granted);
		OnResearchProgress.Broadcast(Granted, 1.f);
	}
}

void USurv_KnowledgeComponent::CancelResearch()
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ResearchTimerHandle);
	}
	ActiveResearchTechTag = FGameplayTag();
	ResearchStartWorldTime = 0.f;
	ResearchCompleteWorldTime = 0.f;
}

// ---- Client intent ----

void USurv_KnowledgeComponent::ServerRequestResearch_Implementation(FGameplayTag TechNodeTag)
{
	// Server re-validates everything inside BeginResearch (prereqs, idle, cost).
	BeginResearch(TechNodeTag);
}

bool USurv_KnowledgeComponent::ServerRequestResearch_Validate(FGameplayTag TechNodeTag)
{
	// Cheap shape check; full validation is authoritative in BeginResearch.
	return TechNodeTag.IsValid();
}

// ---- OnRep ----

void USurv_KnowledgeComponent::OnRep_KnownTech()
{
	BroadcastTechDeltas();
	NotifyKnowledgeChanged();
}

void USurv_KnowledgeComponent::OnRep_Discovered()
{
	BroadcastRecipeDeltas();
	NotifyKnowledgeChanged();
}

void USurv_KnowledgeComponent::OnRep_ActiveResearch()
{
	// Drive a one-shot progress event so UI can latch the new (or cleared) research target.
	OnResearchProgress.Broadcast(ActiveResearchTechTag, GetActiveResearchProgress());
}

void USurv_KnowledgeComponent::BroadcastTechDeltas()
{
	for (const FGameplayTag& Tag : KnownTechTags)
	{
		if (!LastBroadcastTech.HasTagExact(Tag))
		{
			OnTechGranted.Broadcast(Tag);
		}
	}
	LastBroadcastTech = KnownTechTags;
}

void USurv_KnowledgeComponent::BroadcastRecipeDeltas()
{
	for (const FGameplayTag& Tag : DiscoveredRecipeTags)
	{
		if (!LastBroadcastRecipes.HasTagExact(Tag))
		{
			OnRecipeDiscovered.Broadcast(Tag);
		}
	}
	LastBroadcastRecipes = DiscoveredRecipeTags;
}

void USurv_KnowledgeComponent::NotifyKnowledgeChanged() const
{
	// Local, derived-from-replicated-state notification on the bus (never a command channel).
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SurvNativeTags::Bus_KnowledgeChanged, FInstancedStruct(),
			const_cast<USurv_KnowledgeComponent*>(this));
	}
}

// ---- ISeam_Persistable ----

void USurv_KnowledgeComponent::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FSurv_KnowledgeSaveRecord Record;
	Record.KnownTechTags = KnownTechTags;
	Record.DiscoveredRecipeTags = DiscoveredRecipeTags;
	Out.InitializeAs<FSurv_KnowledgeSaveRecord>(Record);
}

void USurv_KnowledgeComponent::RestoreState_Implementation(const FInstancedStruct& In)
{
	// Authority-guarded: a client-side load is a no-op (server replicates the truth back down).
	if (!HasAuthorityToMutate())
	{
		return;
	}
	if (const FSurv_KnowledgeSaveRecord* Record = In.GetPtr<FSurv_KnowledgeSaveRecord>())
	{
		KnownTechTags = Record->KnownTechTags;
		DiscoveredRecipeTags = Record->DiscoveredRecipeTags;
		LastBroadcastTech = KnownTechTags;
		LastBroadcastRecipes = DiscoveredRecipeTags;
		NotifyKnowledgeChanged();
	}
}

FGameplayTag USurv_KnowledgeComponent::GetPersistenceKind_Implementation() const
{
	return SurvNativeTags::Persist_Knowledge;
}
