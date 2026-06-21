// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Logic/Narr_Condition_Gates.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Reputation/Seam_Reputation.h"
#include "Economy/Seam_Wallet.h"
#include "Inventory/Seam_ItemQuery.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace Narr_GateImpl
{
	/** Resolve a registered service implementing the given UInterface from the locator via WorldContext. */
	static UObject* ResolveService(const UObject* WorldContext, const FGameplayTag& ServiceKey)
	{
		if (!WorldContext || !ServiceKey.IsValid())
		{
			return nullptr;
		}
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContext))
		{
			return Locator->ResolveService(ServiceKey);
		}
		return nullptr;
	}

	/**
	 * Resolve the conversing player's pawn/controller as the subject of a possession gate. The condition
	 * Source is the (subject-agnostic) story director, so item/currency gates — which are about the local
	 * player — resolve the first local player's pawn, then its controller, as the seam holder.
	 */
	static AActor* ResolveLocalSubject(const UObject* WorldContext)
	{
		if (!WorldContext)
		{
			return nullptr;
		}
		const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		if (!World)
		{
			return nullptr;
		}
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				return Pawn;
			}
			return PC;
		}
		return nullptr;
	}

	/** Find a UObject implementing UInterfaceClass on Subject (a component first, then the actor). */
	static UObject* FindSeamHolder(AActor* Subject, TSubclassOf<UInterface> UInterfaceClass)
	{
		if (!Subject || !UInterfaceClass)
		{
			return nullptr;
		}
		if (UActorComponent* Comp = Subject->FindComponentByInterface(UInterfaceClass))
		{
			return Comp;
		}
		if (Subject->GetClass()->ImplementsInterface(UInterfaceClass))
		{
			return Subject;
		}
		return nullptr;
	}
}

// ===== UNarr_Condition_Reputation ===============================================================

bool UNarr_Condition_Reputation::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	UObject* WorldContext = Source.GetObject();
	if (!WorldContext || !FactionOrNpcTag.IsValid())
	{
		return Finalize(bDefaultWhenNoSource);
	}

	static const FGameplayTag RepService =
		FGameplayTag::RequestGameplayTag(TEXT("DP.Service.Narrative.Reputation"), /*bErrorIfNotFound=*/false);
	UObject* Provider = Narr_GateImpl::ResolveService(WorldContext, RepService);
	ISeam_Reputation* Rep = Provider ? Cast<ISeam_Reputation>(Provider) : nullptr;
	if (!Rep)
	{
		// No reputation system -> fail closed (a standing gate without standing is unsatisfiable).
		UE_LOG(LogDP, Verbose, TEXT("UNarr_Condition_Reputation: no reputation provider; failing closed."));
		return Finalize(false);
	}

	AActor* Subject = Narr_GateImpl::ResolveLocalSubject(WorldContext);
	if (!Rep->HasReputation(Subject))
	{
		return Finalize(false);
	}
	return Finalize(Rep->MeetsStanding(Subject, FactionOrNpcTag, MinStanding));
}

FString UNarr_Condition_Reputation::DescribeCondition() const
{
	return FString::Printf(TEXT("%sReputation(%s >= %d)"),
		bInvert ? TEXT("!") : TEXT(""), *FactionOrNpcTag.ToString(), MinStanding);
}

// ===== UNarr_Condition_HasCurrency ==============================================================

bool UNarr_Condition_HasCurrency::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	UObject* WorldContext = Source.GetObject();
	if (!WorldContext || !CurrencyTag.IsValid())
	{
		return Finalize(bDefaultWhenNoSource);
	}

	// Prefer a wallet registered as a service; fall back to the local subject's wallet component.
	static const FGameplayTag WalletService =
		FGameplayTag::RequestGameplayTag(TEXT("DP.Service.Economy.Wallet"), /*bErrorIfNotFound=*/false);
	UObject* Holder = Narr_GateImpl::ResolveService(WorldContext, WalletService);
	if (!Holder)
	{
		AActor* Subject = Narr_GateImpl::ResolveLocalSubject(WorldContext);
		Holder = Narr_GateImpl::FindSeamHolder(Subject, USeam_Wallet::StaticClass());
	}
	if (!Holder)
	{
		return Finalize(false);
	}

	const bool bCanAfford = ISeam_Wallet::Execute_CanAfford(Holder, CurrencyTag, Amount);
	return Finalize(bCanAfford);
}

FString UNarr_Condition_HasCurrency::DescribeCondition() const
{
	return FString::Printf(TEXT("%sHasCurrency(%s >= %lld)"),
		bInvert ? TEXT("!") : TEXT(""), *CurrencyTag.ToString(), Amount);
}

// ===== UNarr_Condition_HasItem ==================================================================

bool UNarr_Condition_HasItem::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	UObject* WorldContext = Source.GetObject();
	if (!WorldContext || !ItemTag.IsValid())
	{
		return Finalize(bDefaultWhenNoSource);
	}

	AActor* Subject = Narr_GateImpl::ResolveLocalSubject(WorldContext);
	UObject* Holder = Narr_GateImpl::FindSeamHolder(Subject, USeam_ItemQuery::StaticClass());
	if (!Holder)
	{
		// Also try a service-registered item-query provider.
		static const FGameplayTag ItemService =
			FGameplayTag::RequestGameplayTag(TEXT("DP.Service.Inventory.ItemQuery"), /*bErrorIfNotFound=*/false);
		Holder = Narr_GateImpl::ResolveService(WorldContext, ItemService);
	}
	if (!Holder)
	{
		return Finalize(false);
	}

	const bool bHas = ISeam_ItemQuery::Execute_HasItem(Holder, ItemTag, RequiredAmount);
	return Finalize(bHas);
}

FString UNarr_Condition_HasItem::DescribeCondition() const
{
	return FString::Printf(TEXT("%sHasItem(%s x%d)"),
		bInvert ? TEXT("!") : TEXT(""), *ItemTag.ToString(), RequiredAmount);
}

// ===== UNarr_Condition_SkillCheck ===============================================================

bool UNarr_Condition_SkillCheck::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	// Reads through the FROZEN source facade (QueryCounter already exists) — no new seam needed.
	INarr_StoryConditionSource* Src = GetSource(Source);
	if (!Src || !SkillKey.IsValid())
	{
		return Finalize(bDefaultWhenNoSource);
	}
	const int64 Value = Src->QueryCounter(SkillKey, /*Default=*/0);
	return Finalize(Value >= Difficulty);
}

FString UNarr_Condition_SkillCheck::DescribeCondition() const
{
	return FString::Printf(TEXT("%sSkillCheck(%s >= %lld)"),
		bInvert ? TEXT("!") : TEXT(""), *SkillKey.ToString(), Difficulty);
}
