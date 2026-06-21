// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crafting/USurv_AdvancedCraftingComponent.h"
#include "Crafting/USurv_KnowledgeComponent.h"
#include "Crafting/USurv_RecipeAdvancedDef.h"
#include "Crafting/USurv_Recipe.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "DesignPatternsSurvivalTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "Surv_AdvancedCrafting"

USurv_AdvancedCraftingComponent::USurv_AdvancedCraftingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USurv_AdvancedCraftingComponent::BeginPlay()
{
	Super::BeginPlay();

	// Bind to the base completion delegate exactly once. The base deposits its guaranteed Outputs
	// BEFORE broadcasting OnCraftCompleted, so by the time we run the store already holds the floor.
	if (!bBoundCompleted)
	{
		OnCraftCompleted.AddDynamic(this, &USurv_AdvancedCraftingComponent::HandleAdvancedCompleted);
		bBoundCompleted = true;
	}
}

void USurv_AdvancedCraftingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundCompleted)
	{
		OnCraftCompleted.RemoveDynamic(this, &USurv_AdvancedCraftingComponent::HandleAdvancedCompleted);
		bBoundCompleted = false;
	}
	Super::EndPlay(EndPlayReason);
}

USurv_RecipeAdvancedDef* USurv_AdvancedCraftingComponent::ResolveAdvanced(const FGameplayTag& RecipeTag) const
{
	if (!RecipeTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		// Advanced defs share the recipe's DataTag but live in their own registry bucket. The registry
		// keys by DataTag, so when both a recipe and its advanced def carry the same tag the caller must
		// resolve by concrete type; Find<T> casts and returns null when the indexed asset isn't a
		// USurv_RecipeAdvancedDef (graceful absence).
		return Registry->Find<USurv_RecipeAdvancedDef>(RecipeTag);
	}
	return nullptr;
}

bool USurv_AdvancedCraftingComponent::PassesKnowledgeGate(const USurv_RecipeAdvancedDef* Advanced, FText& OutReason) const
{
	if (!Advanced)
	{
		return true; // no advanced def => no gate
	}
	if (Advanced->GatingTechTag.IsValid())
	{
		if (!Knowledge || !Knowledge->HasTech(Advanced->GatingTechTag))
		{
			OutReason = LOCTEXT("LockedTech", "Required technology not yet researched.");
			return false;
		}
	}
	if (Advanced->bRequiresDiscovery)
	{
		if (!Knowledge || !Knowledge->IsRecipeUnlocked(Advanced->DataTag))
		{
			OutReason = LOCTEXT("Undiscovered", "Recipe not yet discovered.");
			return false;
		}
	}
	return true;
}

bool USurv_AdvancedCraftingComponent::CanCraftRecipe(FGameplayTag RecipeTag, FText& OutReason) const
{
	OutReason = FText::GetEmpty();

	const USurv_Recipe* Recipe = ResolveRecipe(RecipeTag);
	if (!Recipe)
	{
		OutReason = LOCTEXT("NoRecipe", "Unknown recipe.");
		return false;
	}

	// Tech / discovery gate (advanced extension, advisory).
	const USurv_RecipeAdvancedDef* Advanced = ResolveAdvanced(RecipeTag);
	if (!PassesKnowledgeGate(Advanced, OutReason))
	{
		return false;
	}

	// Station gate (reuses the base hierarchy-aware check).
	if (!HasRequiredStation(Recipe))
	{
		OutReason = LOCTEXT("NoStation", "Required crafting station not available.");
		return false;
	}

	// Input affordability (reuses the base check against the store).
	if (!CanAfford(Recipe))
	{
		OutReason = LOCTEXT("NoInputs", "Insufficient materials.");
		return false;
	}

	return true;
}

TArray<FGameplayTag> USurv_AdvancedCraftingComponent::GetCraftableRecipes() const
{
	TArray<FGameplayTag> Result;
	FText Unused;
	for (const FGameplayTag& Tag : OfferedRecipeTags)
	{
		if (CanCraftRecipe(Tag, Unused))
		{
			Result.Add(Tag);
		}
	}
	return Result;
}

void USurv_AdvancedCraftingComponent::ServerStartCraft_Implementation(FGameplayTag RecipeTag)
{
	// Server-authoritative gate, then defer to the base queue logic.
	FText Reason;
	if (!CanCraftRecipe(RecipeTag, Reason))
	{
		UE_LOG(LogDP, Verbose, TEXT("[Survival] ServerStartCraft rejected %s: %s"),
			*RecipeTag.ToString(), *Reason.ToString());
		return;
	}
	StartCraft(RecipeTag);
}

bool USurv_AdvancedCraftingComponent::ServerStartCraft_Validate(FGameplayTag RecipeTag)
{
	return RecipeTag.IsValid();
}

int32 USurv_AdvancedCraftingComponent::SelectQualityTier(const USurv_RecipeAdvancedDef* Advanced, FRandomStream& Stream) const
{
	if (!Advanced || Advanced->QualityTiers.Num() == 0)
	{
		return INDEX_NONE;
	}
	float TotalWeight = 0.f;
	for (const FSurv_QualityTier& Tier : Advanced->QualityTiers)
	{
		TotalWeight += FMath::Max(0.f, Tier.SelectionWeight);
	}
	if (TotalWeight <= 0.f)
	{
		return INDEX_NONE;
	}
	float Roll = Stream.FRandRange(0.f, TotalWeight);
	for (int32 i = 0; i < Advanced->QualityTiers.Num(); ++i)
	{
		Roll -= FMath::Max(0.f, Advanced->QualityTiers[i].SelectionWeight);
		if (Roll <= 0.f)
		{
			return i;
		}
	}
	return Advanced->QualityTiers.Num() - 1;
}

void USurv_AdvancedCraftingComponent::HandleAdvancedCompleted(FGameplayTag RecipeTag)
{
	// AUTHORITY-only: only the server mutates the store. Clients receive the deposit via replication.
	if (!HasAuthorityToMutate())
	{
		return;
	}

	const USurv_RecipeAdvancedDef* Advanced = ResolveAdvanced(RecipeTag);
	if (!Advanced || !ResourceStore)
	{
		return; // no advanced layer for this recipe (base Outputs already deposited)
	}

	// Deterministic-ish per-craft stream seeded by recipe + world time so server rolls are reproducible
	// for the same authority frame; this is a defensive default, not a gameplay-tuning magic number.
	const uint32 Seed = HashCombine(GetTypeHash(RecipeTag), GetTypeHash(FMath::FloorToInt(GetWorldTimeSeconds() * 1000.0)));
	FRandomStream Stream(static_cast<int32>(Seed));

	// Roll critical: scales the EXTRA (byproduct + tier) stacks only — never the base Outputs.
	const bool bCritical = (Advanced->CriticalCraftChance > 0.f) && (Stream.FRand() < Advanced->CriticalCraftChance);
	const float ExtraMult = bCritical ? FMath::Max(1.f, Advanced->CriticalExtraMultiplier) : 1.f;

	auto DepositScaled = [this, ExtraMult](const FSurv_ResourceStack& Stack)
	{
		if (Stack.ItemTag.IsValid() && Stack.Count > 0)
		{
			const int32 Amount = FMath::Max(0, FMath::RoundToInt(Stack.Count * ExtraMult));
			if (Amount > 0)
			{
				ResourceStore->AddResource(Stack.ItemTag, Amount);
			}
		}
	};

	// Guaranteed byproducts (scaled on crit).
	for (const FSurv_ResourceStack& By : Advanced->Byproducts)
	{
		DepositScaled(By);
	}

	// Weighted quality tier: additive extra stacks (scaled on crit).
	const int32 TierIdx = SelectQualityTier(Advanced, Stream);
	if (Advanced->QualityTiers.IsValidIndex(TierIdx))
	{
		for (const FSurv_ResourceStack& Extra : Advanced->QualityTiers[TierIdx].ExtraOutputs)
		{
			DepositScaled(Extra);
		}
		UE_LOG(LogDP, Verbose, TEXT("[Survival] Advanced craft %s -> quality %s%s"),
			*RecipeTag.ToString(),
			*Advanced->QualityTiers[TierIdx].QualityTag.ToString(),
			bCritical ? TEXT(" (CRITICAL)") : TEXT(""));
	}

	if (bCritical)
	{
		NotifyCriticalCraft(RecipeTag);
	}
}

void USurv_AdvancedCraftingComponent::NotifyCriticalCraft(const FGameplayTag& RecipeTag) const
{
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SurvNativeTags::Bus_CraftCritical, FInstancedStruct(),
			const_cast<USurv_AdvancedCraftingComponent*>(this));
	}
}

#undef LOCTEXT_NAMESPACE
