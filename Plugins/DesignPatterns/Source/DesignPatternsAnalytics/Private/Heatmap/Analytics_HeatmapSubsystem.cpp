// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Heatmap/Analytics_HeatmapSubsystem.h"

#include "Subsystem/Analytics_Subsystem.h"
#include "Settings/Analytics_TelemetrySettings.h"
#include "Data/Analytics_TelemetryDataAsset.h"
#include "Tags/Analytics_TelemetryTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Net/Seam_NetValue.h"

#include "Engine/GameInstance.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"

namespace
{
	const FName GAttr_Category(TEXT("category"));
	const FName GAttr_BucketCount(TEXT("buckets"));
}

void UAnalytics_HeatmapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Log, TEXT("Analytics HeatmapSubsystem initialized."));
}

void UAnalytics_HeatmapSubsystem::Deinitialize()
{
	// Final SYNCHRONOUS export of every non-empty category so a world teardown does not drop data.
	// We deliberately write synchronously here: the worker-thread Async path is unsafe during
	// teardown (the subsystem and its data may be gone before a queued task runs), so we snapshot
	// and write inline. Snapshots are plain values; no UObject graph is touched.
	for (const TPair<FGameplayTag, FHeatGrid>& Pair : Categories)
	{
		if (Pair.Value.Cells.Num() == 0)
		{
			continue;
		}
		TArray<FAnalytics_HeatBucket> Snapshot;
		SnapshotCategory(Pair.Key, Snapshot);
		const FString Path = BuildExportPath(Pair.Key);
		WriteSnapshot(Snapshot, Path, /*bSynchronous*/ true);
	}

	Categories.Reset();
	CachedAnalyticsSubsystem.Reset();
	CachedDataAsset.Reset();

	Super::Deinitialize();
}

UAnalytics_Subsystem* UAnalytics_HeatmapSubsystem::ResolveAnalyticsSubsystem()
{
	if (UAnalytics_Subsystem* Cached = CachedAnalyticsSubsystem.Get())
	{
		return Cached;
	}
	// GetGameInstance null-guard: a world subsystem may outlive a clean GI in odd teardown orders.
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			UAnalytics_Subsystem* Sub = GI->GetSubsystem<UAnalytics_Subsystem>();
			CachedAnalyticsSubsystem = Sub;
			return Sub;
		}
	}
	return nullptr;
}

const UAnalytics_TelemetryDataAsset* UAnalytics_HeatmapSubsystem::ResolveDataAsset()
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

void UAnalytics_HeatmapSubsystem::RecordSpatialEvent(FGameplayTag CategoryTag, const FVector& WorldPos, int32 Weight)
{
	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	if (Settings && !Settings->bEnableHeatmap)
	{
		return;
	}
	if (!CategoryTag.IsValid())
	{
		return;
	}

	const int32 EffectiveWeight = Weight > 0 ? Weight : 1; // defensive

	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const float BucketSize = Data ? Data->GetEffectiveBucketSizeUU() : 500.f; // floored >= 1
	const int32 MaxBuckets = Data ? Data->GetEffectiveHeatmapMaxBuckets() : 8192;

	const int32 CellX = FMath::FloorToInt(static_cast<float>(WorldPos.X) / BucketSize);
	const int32 CellY = FMath::FloorToInt(static_cast<float>(WorldPos.Y) / BucketSize);
	const uint64 Key = MakeCellKey(CellX, CellY);

	FHeatGrid& Grid = Categories.FindOrAdd(CategoryTag);
	if (FAnalytics_HeatBucket* Existing = Grid.Cells.Find(Key))
	{
		Existing->Count += EffectiveWeight;
		return;
	}

	// New cell: only add while under the cap (bounded memory).
	if (Grid.Cells.Num() >= MaxBuckets)
	{
		UE_LOG(LogDP, Verbose, TEXT("Heatmap category '%s' at bucket cap %d; dropping new cell."),
			*CategoryTag.ToString(), MaxBuckets);
		return;
	}

	FAnalytics_HeatBucket NewBucket;
	NewBucket.X = CellX;
	NewBucket.Y = CellY;
	NewBucket.Count = EffectiveWeight;
	Grid.Cells.Add(Key, NewBucket);
}

int32 UAnalytics_HeatmapSubsystem::GetBucketCount(FGameplayTag CategoryTag, const FVector& WorldPos) const
{
	const FHeatGrid* Grid = Categories.Find(CategoryTag);
	if (!Grid)
	{
		return 0;
	}
	// Recompute the cell using the same (possibly defaulted) bucket size as RecordSpatialEvent.
	float BucketSize = 500.f;
	if (const UAnalytics_TelemetryDataAsset* Data = CachedDataAsset.Get())
	{
		BucketSize = Data->GetEffectiveBucketSizeUU();
	}
	const int32 CellX = FMath::FloorToInt(static_cast<float>(WorldPos.X) / BucketSize);
	const int32 CellY = FMath::FloorToInt(static_cast<float>(WorldPos.Y) / BucketSize);
	const FAnalytics_HeatBucket* Bucket = Grid->Cells.Find(MakeCellKey(CellX, CellY));
	return Bucket ? Bucket->Count : 0;
}

void UAnalytics_HeatmapSubsystem::ClearCategory(FGameplayTag CategoryTag)
{
	Categories.Remove(CategoryTag);
}

void UAnalytics_HeatmapSubsystem::SnapshotCategory(const FGameplayTag& CategoryTag, TArray<FAnalytics_HeatBucket>& OutBuckets) const
{
	OutBuckets.Reset();
	if (const FHeatGrid* Grid = Categories.Find(CategoryTag))
	{
		OutBuckets.Reserve(Grid->Cells.Num());
		for (const TPair<uint64, FAnalytics_HeatBucket>& Cell : Grid->Cells)
		{
			OutBuckets.Add(Cell.Value);
		}
	}
}

FString UAnalytics_HeatmapSubsystem::BuildExportPath(const FGameplayTag& CategoryTag) const
{
	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	// Reuse the analytics subdir convention; heatmaps go under a Heatmap subfolder.
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Analytics"), TEXT("Heatmap"));

	// Category tag is sanitised into a filename-safe token.
	FString SafeCategory = CategoryTag.ToString();
	SafeCategory.ReplaceInline(TEXT("."), TEXT("_"));
	const FString FileName = FString::Printf(TEXT("heat_%s_%s.csv"),
		*SafeCategory, *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
	return FPaths::Combine(Dir, FileName);
}

void UAnalytics_HeatmapSubsystem::WriteSnapshot(const TArray<FAnalytics_HeatBucket>& Snapshot, const FString& FullPath, bool bSynchronous)
{
	if (Snapshot.Num() == 0)
	{
		return;
	}

	// The actual write captures ONLY the plain-value snapshot + a string path by value; never 'this'.
	auto WriteFn = [Snapshot, FullPath]()
	{
		const FString Dir = FPaths::GetPath(FullPath);
		IFileManager& FM = IFileManager::Get();
		if (!FM.DirectoryExists(*Dir))
		{
			FM.MakeDirectory(*Dir, /*Tree*/ true);
		}

		FString Csv = TEXT("x,y,count\n");
		Csv.Reserve(Snapshot.Num() * 16);
		for (const FAnalytics_HeatBucket& Bucket : Snapshot)
		{
			Csv += FString::Printf(TEXT("%d,%d,%d\n"), Bucket.X, Bucket.Y, Bucket.Count);
		}

		const bool bOk = FFileHelper::SaveStringToFile(
			Csv, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (!bOk)
		{
			UE_LOG(LogDP, Warning, TEXT("Heatmap export failed to write %s"), *FullPath);
		}
	};

	if (bSynchronous)
	{
		WriteFn();
	}
	else
	{
		Async(EAsyncExecution::ThreadPool, MoveTemp(WriteFn));
	}
}

void UAnalytics_HeatmapSubsystem::ExportHeatmap(FGameplayTag CategoryTag)
{
	const FHeatGrid* Grid = Categories.Find(CategoryTag);
	if (!Grid || Grid->Cells.Num() == 0)
	{
		return;
	}

	// Game-thread snapshot, then off-thread write (capture by value).
	TArray<FAnalytics_HeatBucket> Snapshot;
	SnapshotCategory(CategoryTag, Snapshot);
	const FString Path = BuildExportPath(CategoryTag);
	WriteSnapshot(Snapshot, Path, /*bSynchronous*/ false);

	// Record an aggregate export marker through the consent-gated core subsystem.
	if (UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem())
	{
		TArray<FSeam_AnalyticsAttr> Attrs;
		Attrs.Emplace(GAttr_Category, FSeam_NetValue::MakeTag(CategoryTag));
		Attrs.Emplace(GAttr_BucketCount, FSeam_NetValue::MakeInt(Snapshot.Num()));
		Analytics->RecordEvent(AnalyticsTelemetryTags::Event_Heatmap_Export, Attrs);
	}
}

FString UAnalytics_HeatmapSubsystem::GetDPDebugString_Implementation() const
{
	int32 TotalCells = 0;
	for (const TPair<FGameplayTag, FHeatGrid>& Pair : Categories)
	{
		TotalCells += Pair.Value.Cells.Num();
	}
	return FString::Printf(TEXT("Heatmap: categories=%d cells=%d"), Categories.Num(), TotalCells);
}
