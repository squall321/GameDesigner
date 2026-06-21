// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Reward/SimEco_QuestRewardComponent.h"
#include "Pricing/SimEco_PricingTags.h"
#include "Economy/Seam_WalletAuthority.h"
#include "Economy/Seam_PurchaseTarget.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

USimEco_QuestRewardComponent::USimEco_QuestRewardComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USimEco_QuestRewardComponent::BeginPlay()
{
	Super::BeginPlay();

	// Register as the reward-sink service so quests/achievements resolve us by tag. WeakObserved: the
	// component is owned by its actor, not the GameInstance locator.
	if (UDP_ServiceLocatorSubsystem* Locator = ResolveLocator())
	{
		bRegistered = Locator->RegisterService(
			SimEcoPricingTags::Service_RewardSink, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}
}

void USimEco_QuestRewardComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (UDP_ServiceLocatorSubsystem* Locator = ResolveLocator())
		{
			Locator->UnregisterService(SimEcoPricingTags::Service_RewardSink);
		}
		bRegistered = false;
	}
	Super::EndPlay(EndPlayReason);
}

bool USimEco_QuestRewardComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

UDP_ServiceLocatorSubsystem* USimEco_QuestRewardComponent::ResolveLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

UObject* USimEco_QuestRewardComponent::ResolveSeam(const AActor* Receiver, TSubclassOf<UInterface> SeamClass)
{
	if (!Receiver || !*SeamClass)
	{
		return nullptr;
	}
	if (Receiver->GetClass()->ImplementsInterface(SeamClass))
	{
		return const_cast<AActor*>(Receiver);
	}
	return const_cast<AActor*>(Receiver)->FindComponentByInterface(SeamClass);
}

bool USimEco_QuestRewardComponent::PayReward_Implementation(AActor* Receiver, const FSeam_RewardSpec& Spec)
{
	if (!HasAuthority())
	{
		return false;
	}
	if (!Receiver || !Spec.IsValidSpec())
	{
		return false;
	}

	UObject* Wallet = ResolveSeam(Receiver, USeam_WalletAuthority::StaticClass());
	UObject* Inventory = ResolveSeam(Receiver, USeam_PurchaseTarget::StaticClass());

	// First validate that every line CAN be paid (so we don't half-pay). Currency is always grantable
	// (wallets clamp, never reject a non-negative grant), so we only pre-check item capacity.
	for (const FSeam_RewardLine& Line : Spec.Lines)
	{
		if (!Line.IsValidLine())
		{
			continue;
		}
		if (!Line.bIsCurrency)
		{
			if (!Inventory || !ISeam_PurchaseTarget::Execute_CanReceive(Inventory, Line.CurrencyOrItemTag, (int32)Line.GetAmount()))
			{
				UE_LOG(LogDP, Warning, TEXT("[QuestReward] Cannot pay item line %s (no inventory / full)"),
					*Line.CurrencyOrItemTag.ToString());
				return false;
			}
		}
		else if (!Wallet)
		{
			UE_LOG(LogDP, Warning, TEXT("[QuestReward] Cannot pay currency line %s (no wallet)"),
				*Line.CurrencyOrItemTag.ToString());
			return false;
		}
	}

	// All lines validated; pay them.
	bool bAllPaid = true;
	for (const FSeam_RewardLine& Line : Spec.Lines)
	{
		if (!Line.IsValidLine())
		{
			continue;
		}
		if (Line.bIsCurrency)
		{
			const int64 Granted = ISeam_WalletAuthority::Execute_Grant(Wallet, Line.CurrencyOrItemTag, Line.GetAmount());
			bAllPaid &= (Granted > 0);
		}
		else
		{
			const int32 Granted = ISeam_PurchaseTarget::Execute_GrantItem(Inventory, Line.CurrencyOrItemTag, (int32)Line.GetAmount());
			bAllPaid &= (Granted >= (int32)Line.GetAmount());
		}
	}

	// After-the-fact notification (never a command).
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SimEcoPricingTags::Bus_RewardPaid, FInstancedStruct(), Receiver);
	}

	UE_LOG(LogDP, Verbose, TEXT("[QuestReward] Paid reward from %s to %s (allPaid=%d)"),
		*Spec.SourceTag.ToString(), *Receiver->GetName(), bAllPaid ? 1 : 0);
	return bAllPaid;
}

bool USimEco_QuestRewardComponent::PayCurrency(AActor* Receiver, FGameplayTag SourceTag, FGameplayTag CurrencyTag, int64 Amount)
{
	FSeam_RewardSpec Spec;
	Spec.SourceTag = SourceTag;
	Spec.Lines.Add(FSeam_RewardLine::Currency(CurrencyTag, Amount));
	return PayReward_Implementation(Receiver, Spec);
}

bool USimEco_QuestRewardComponent::PayItem(AActor* Receiver, FGameplayTag SourceTag, FGameplayTag ItemTag, int32 Count)
{
	FSeam_RewardSpec Spec;
	Spec.SourceTag = SourceTag;
	Spec.Lines.Add(FSeam_RewardLine::Item(ItemTag, Count));
	return PayReward_Implementation(Receiver, Spec);
}
