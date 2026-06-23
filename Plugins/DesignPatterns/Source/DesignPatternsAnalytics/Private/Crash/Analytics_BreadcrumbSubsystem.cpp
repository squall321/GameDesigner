// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crash/Analytics_BreadcrumbSubsystem.h"

#include "Subsystem/Analytics_Subsystem.h"
#include "Settings/Analytics_TelemetrySettings.h"
#include "Data/Analytics_TelemetryDataAsset.h"
#include "Tags/Analytics_TelemetryTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Net/Seam_NetValue.h"

#include "Engine/GameInstance.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/App.h"

namespace
{
	const FName GAttr_Channel(TEXT("channel"));
	const FName GAttr_Reason(TEXT("reason"));
	const FName GAttr_Count(TEXT("count"));

	/** Serialize one breadcrumb to a single text line for the crash-context file. */
	FString CrumbToLine(const FAnalytics_Breadcrumb& Crumb)
	{
		FString Line = FString::Printf(TEXT("[%.3f] %s"), Crumb.TimestampSeconds, *Crumb.Tag.ToString());
		for (const FSeam_AnalyticsAttr& Attr : Crumb.Attrs)
		{
			Line += FString::Printf(TEXT(" %s="), *Attr.Key.ToString());
			switch (Attr.Value.Type)
			{
			case ESeam_NetValueType::Bool:   Line += Attr.Value.bValue ? TEXT("true") : TEXT("false"); break;
			case ESeam_NetValueType::Int:    Line += FString::Printf(TEXT("%lld"), Attr.Value.IntValue); break;
			case ESeam_NetValueType::Float:  Line += FString::Printf(TEXT("%f"), Attr.Value.FloatValue); break;
			case ESeam_NetValueType::Vector: Line += Attr.Value.VectorValue.ToString(); break;
			case ESeam_NetValueType::Tag:    Line += Attr.Value.TagValue.ToString(); break;
			case ESeam_NetValueType::Name:   Line += Attr.Value.NameValue.ToString(); break;
			default:                         Line += TEXT("null"); break;
			}
		}
		return Line;
	}
}

void UAnalytics_BreadcrumbSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	if (Settings && !Settings->bEnableBreadcrumb)
	{
		UE_LOG(LogDP, Log, TEXT("Analytics BreadcrumbSubsystem disabled by settings."));
		return;
	}

	MessageBus = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_MessageBusSubsystem>() : nullptr;
	SubscribeToBus();

	UE_LOG(LogDP, Log, TEXT("Analytics BreadcrumbSubsystem initialized."));
}

void UAnalytics_BreadcrumbSubsystem::Deinitialize()
{
	if (MessageBus && BusListenerHandle.IsValid())
	{
		MessageBus->StopListeningForOwner(this);
		BusListenerHandle = FDP_ListenerHandle();
	}

	Ring.Reset();
	MessageBus = nullptr;
	CachedAnalyticsSubsystem.Reset();
	CachedDataAsset.Reset();

	Super::Deinitialize();
}

UAnalytics_Subsystem* UAnalytics_BreadcrumbSubsystem::ResolveAnalyticsSubsystem()
{
	if (UAnalytics_Subsystem* Cached = CachedAnalyticsSubsystem.Get())
	{
		return Cached;
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		UAnalytics_Subsystem* Sub = GI->GetSubsystem<UAnalytics_Subsystem>();
		CachedAnalyticsSubsystem = Sub;
		return Sub;
	}
	return nullptr;
}

const UAnalytics_TelemetryDataAsset* UAnalytics_BreadcrumbSubsystem::ResolveDataAsset()
{
	if (const UAnalytics_TelemetryDataAsset* Cached = CachedDataAsset.Get())
	{
		return Cached;
	}
	if (bDataAssetResolutionAttempted)
	{
		return nullptr;
	}
	bDataAssetResolutionAttempted = true;

	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	if (!Settings || !Settings->TelemetryDataTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		const UAnalytics_TelemetryDataAsset* Asset = Registry->Find<UAnalytics_TelemetryDataAsset>(Settings->TelemetryDataTag);
		CachedDataAsset = Asset;
		return Asset;
	}
	return nullptr;
}

void UAnalytics_BreadcrumbSubsystem::SubscribeToBus()
{
	if (!MessageBus)
	{
		return;
	}

	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	const FGameplayTag Channel = Settings ? Settings->BreadcrumbBusChannelToObserve : FGameplayTag();
	if (!Channel.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("Breadcrumb: no observed bus channel; auto-crumb bridge inactive."));
		return;
	}

	TWeakObjectPtr<UAnalytics_BreadcrumbSubsystem> WeakThis(this);
	BusListenerHandle = MessageBus->ListenNative(
		Channel,
		[WeakThis](const FDP_Message& Message)
		{
			if (UAnalytics_BreadcrumbSubsystem* Self = WeakThis.Get())
			{
				Self->HandleBusMessage(Message);
			}
		},
		this,
		EDP_MessageMatch::ExactOrChild);

	UE_LOG(LogDP, Log, TEXT("Breadcrumb observing bus channel '%s'."), *Channel.ToString());
}

void UAnalytics_BreadcrumbSubsystem::HandleBusMessage(const FDP_Message& Message)
{
	if (!Message.Channel.IsValid())
	{
		return;
	}
	// The crumb tag IS the source channel; carry nothing else (payload may not be PII-safe).
	LeaveSimple(Message.Channel);
}

void UAnalytics_BreadcrumbSubsystem::EnforceRingCap()
{
	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const int32 Cap = Data ? Data->GetEffectiveBreadcrumbRingSize() : 64;
	if (Ring.Num() <= Cap)
	{
		return;
	}
	const int32 Overflow = Ring.Num() - Cap;
	Ring.RemoveAt(0, Overflow, /*bAllowShrinking*/ false);
}

void UAnalytics_BreadcrumbSubsystem::Leave(FGameplayTag CrumbTag, const TArray<FSeam_AnalyticsAttr>& Attrs)
{
	if (!CrumbTag.IsValid())
	{
		return;
	}
	FAnalytics_Breadcrumb& Crumb = Ring.AddDefaulted_GetRef();
	Crumb.Tag = CrumbTag;
	Crumb.TimestampSeconds = FApp::GetCurrentTime();
	Crumb.Attrs = Attrs; // PII-safe FSeam_NetValue values, copied by value
	EnforceRingCap();
}

void UAnalytics_BreadcrumbSubsystem::LeaveSimple(FGameplayTag CrumbTag)
{
	static const TArray<FSeam_AnalyticsAttr> Empty;
	Leave(CrumbTag, Empty);
}

TArray<FAnalytics_Breadcrumb> UAnalytics_BreadcrumbSubsystem::GetTrail() const
{
	return Ring;
}

void UAnalytics_BreadcrumbSubsystem::AttachToReport(FGameplayTag ReportReason)
{
	if (Ring.Num() == 0)
	{
		return;
	}

	// Game-thread snapshot, then off-thread write (capture BY VALUE; never 'this').
	const TArray<FAnalytics_Breadcrumb> Snapshot = Ring;

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"), TEXT("Crash"));
	FString SafeReason = ReportReason.IsValid() ? ReportReason.ToString() : FString(TEXT("report"));
	SafeReason.ReplaceInline(TEXT("."), TEXT("_"));
	const FString FullPath = FPaths::Combine(Dir,
		FString::Printf(TEXT("breadcrumbs_%s_%s.txt"), *SafeReason,
			*FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))));

	Async(EAsyncExecution::ThreadPool, [Snapshot, Dir, FullPath]()
	{
		IFileManager& FM = IFileManager::Get();
		if (!FM.DirectoryExists(*Dir))
		{
			FM.MakeDirectory(*Dir, /*Tree*/ true);
		}

		FString Text;
		Text.Reserve(Snapshot.Num() * 64);
		for (const FAnalytics_Breadcrumb& Crumb : Snapshot)
		{
			Text += CrumbToLine(Crumb);
			Text += LINE_TERMINATOR;
		}

		const bool bOk = FFileHelper::SaveStringToFile(
			Text, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (!bOk)
		{
			UE_LOG(LogDP, Warning, TEXT("Breadcrumb crash-context write failed: %s"), *FullPath);
		}
	});

	// Analytics marker is consent-gated by the core subsystem.
	if (UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem())
	{
		TArray<FSeam_AnalyticsAttr> Attrs;
		if (ReportReason.IsValid())
		{
			Attrs.Emplace(GAttr_Reason, FSeam_NetValue::MakeTag(ReportReason));
		}
		Attrs.Emplace(GAttr_Count, FSeam_NetValue::MakeInt(Snapshot.Num()));
		Analytics->RecordEvent(AnalyticsTelemetryTags::Event_Crash_BreadcrumbAttached, Attrs);
	}
}

FString UAnalytics_BreadcrumbSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Breadcrumb: trail=%d bus=%s"),
		Ring.Num(), BusListenerHandle.IsValid() ? TEXT("subscribed") : TEXT("inactive"));
}
