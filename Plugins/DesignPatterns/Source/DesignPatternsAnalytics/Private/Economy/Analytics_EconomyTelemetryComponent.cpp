// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Economy/Analytics_EconomyTelemetryComponent.h"

#include "Subsystem/Analytics_Subsystem.h"
#include "Tags/Analytics_TelemetryTags.h"

#include "Core/DPLog.h"
#include "Net/Seam_NetValue.h"
#include "Economy/Seam_Wallet.h"

#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"

namespace
{
	const FName GAttr_Resource(TEXT("resource"));
	const FName GAttr_Delta(TEXT("delta"));
	const FName GAttr_Reason(TEXT("reason"));
	const FName GAttr_Net(TEXT("net"));
	const FName GAttr_Balance(TEXT("balance"));
}

UAnalytics_EconomyTelemetryComponent::UAnalytics_EconomyTelemetryComponent()
{
	// Strictly local: no tick, no replication (mirrors UAnalytics_ProgressionComponent).
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

void UAnalytics_EconomyTelemetryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Backstop snapshot so a teardown does not lose the session's accumulated economy data.
	FlushEconomySnapshot();
	WalletProvider = nullptr;
	CachedAnalyticsSubsystem.Reset();
	Super::EndPlay(EndPlayReason);
}

UAnalytics_Subsystem* UAnalytics_EconomyTelemetryComponent::ResolveAnalyticsSubsystem()
{
	if (UAnalytics_Subsystem* Cached = CachedAnalyticsSubsystem.Get())
	{
		return Cached;
	}
	if (const AActor* Owner = GetOwner())
	{
		if (const UWorld* World = Owner->GetWorld())
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				UAnalytics_Subsystem* Sub = GI->GetSubsystem<UAnalytics_Subsystem>();
				CachedAnalyticsSubsystem = Sub;
				return Sub;
			}
		}
	}
	return nullptr;
}

FName UAnalytics_EconomyTelemetryComponent::MakeResourceSourceKey(const FGameplayTag& Resource, const FGameplayTag& SourceSink)
{
	return FName(*FString::Printf(TEXT("%s|%s"), *Resource.ToString(), *SourceSink.ToString()));
}

void UAnalytics_EconomyTelemetryComponent::RecordResourceFlow(FGameplayTag ResourceTag, int64 Delta, FGameplayTag SourceSinkTag)
{
	if (!ResourceTag.IsValid() || Delta == 0)
	{
		return;
	}

	NetFlowByResource.FindOrAdd(ResourceTag) += Delta;
	if (SourceSinkTag.IsValid())
	{
		FlowByResourceSource.FindOrAdd(MakeResourceSourceKey(ResourceTag, SourceSinkTag)) += Delta;
	}

	if (UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem())
	{
		TArray<FSeam_AnalyticsAttr> Attrs;
		Attrs.Emplace(GAttr_Resource, FSeam_NetValue::MakeTag(ResourceTag));
		Attrs.Emplace(GAttr_Delta, FSeam_NetValue::MakeInt(Delta));
		if (SourceSinkTag.IsValid())
		{
			Attrs.Emplace(GAttr_Reason, FSeam_NetValue::MakeTag(SourceSinkTag));
		}
		Analytics->RecordEvent(AnalyticsTelemetryTags::Event_Economy_Flow, Attrs);
	}
}

int64 UAnalytics_EconomyTelemetryComponent::GetNetFlow(FGameplayTag ResourceTag) const
{
	const int64* Found = NetFlowByResource.Find(ResourceTag);
	return Found ? *Found : 0;
}

int64 UAnalytics_EconomyTelemetryComponent::GetSourceTotal(FGameplayTag ResourceTag, FGameplayTag SourceSinkTag) const
{
	const int64* Found = FlowByResourceSource.Find(MakeResourceSourceKey(ResourceTag, SourceSinkTag));
	return Found ? *Found : 0;
}

void UAnalytics_EconomyTelemetryComponent::SetWalletProvider(const TScriptInterface<ISeam_Wallet>& InWallet)
{
	WalletProvider = InWallet;
}

void UAnalytics_EconomyTelemetryComponent::FlushEconomySnapshot()
{
	UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem();
	if (!Analytics)
	{
		return;
	}

	// One snapshot event per resource keeps attributes flat and dashboard-pivotable.
	for (const TPair<FGameplayTag, int64>& Pair : NetFlowByResource)
	{
		TArray<FSeam_AnalyticsAttr> Attrs;
		Attrs.Emplace(GAttr_Resource, FSeam_NetValue::MakeTag(Pair.Key));
		Attrs.Emplace(GAttr_Net, FSeam_NetValue::MakeInt(Pair.Value));

		// Optional live balance from the wallet seam (BlueprintNativeEvent -> Execute_).
		if (UObject* WalletObj = WalletProvider.GetObject())
		{
			const int64 Balance = ISeam_Wallet::Execute_GetBalance(WalletObj, Pair.Key);
			Attrs.Emplace(GAttr_Balance, FSeam_NetValue::MakeInt(Balance));
		}

		Analytics->RecordEvent(AnalyticsTelemetryTags::Event_Economy_Snapshot, Attrs);
	}
}
