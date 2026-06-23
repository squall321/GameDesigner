// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pool/DPWidgetPoolSubsystem.h"
#include "Pool/DPWidgetPoolable.h"
#include "View/DPViewBase.h"
#include "Core/DPLog.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

DECLARE_DWORD_COUNTER_STAT(TEXT("DP Widget Pool Acquires"), STAT_DP_WidgetPool_Acquire, STATGROUP_DesignPatterns);
DECLARE_DWORD_COUNTER_STAT(TEXT("DP Widget Pool Reuses"), STAT_DP_WidgetPool_Reuse, STATGROUP_DesignPatterns);

void UDP_WidgetPoolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDPPool, Verbose, TEXT("[WidgetPool] Initialized."));
}

void UDP_WidgetPoolSubsystem::Deinitialize()
{
	// Release every live + idle widget so nothing dangles past game-instance teardown.
	for (TPair<FDP_WidgetPoolKey, FDP_WidgetPoolBucket>& Pair : Buckets)
	{
		for (TObjectPtr<UUserWidget>& Widget : Pair.Value.Live)
		{
			if (Widget)
			{
				ApplyReleaseReset(Widget);
			}
		}
		Pair.Value.Live.Reset();
		Pair.Value.Idle.Reset();
	}
	Buckets.Reset();

	Super::Deinitialize();
}

APlayerController* UDP_WidgetPoolSubsystem::GetFirstLocalPlayerController() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}
	if (const ULocalPlayer* LP = GI->GetFirstGamePlayer())
	{
		return LP->GetPlayerController(GI->GetWorld());
	}
	return nullptr;
}

FDP_WidgetPoolKey UDP_WidgetPoolSubsystem::MakeKey(TSubclassOf<UUserWidget> WidgetClass, APlayerController* OwningPlayer) const
{
	APlayerController* Player = OwningPlayer ? OwningPlayer : GetFirstLocalPlayerController();
	return FDP_WidgetPoolKey(WidgetClass, Player);
}

UUserWidget* UDP_WidgetPoolSubsystem::CreateNewWidget(const FDP_WidgetPoolKey& Key) const
{
	if (!Key.WidgetClass)
	{
		return nullptr;
	}
	APlayerController* Player = Key.OwningPlayer.Get();
	if (!Player)
	{
		UE_LOG(LogDPPool, Warning, TEXT("[WidgetPool] Cannot create %s: no owning player controller."),
			*Key.WidgetClass->GetName());
		return nullptr;
	}
	// CreateWidget keys the new widget to the owning player so input/viewport ownership is correct.
	return CreateWidget<UUserWidget>(Player, Key.WidgetClass);
}

UUserWidget* UDP_WidgetPoolSubsystem::AcquireWidget(TSubclassOf<UUserWidget> WidgetClass, APlayerController* OwningPlayer)
{
	INC_DWORD_STAT(STAT_DP_WidgetPool_Acquire);

	if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogDPPool, Warning, TEXT("[WidgetPool] AcquireWidget with null/abstract class."));
		return nullptr;
	}

	PruneDeadOwners();

	const FDP_WidgetPoolKey Key = MakeKey(WidgetClass, OwningPlayer);
	FDP_WidgetPoolBucket& Bucket = Buckets.FindOrAdd(Key);

	UUserWidget* Widget = nullptr;

	// Reuse the most-recently-parked idle instance when available.
	while (Bucket.Idle.Num() > 0 && !Widget)
	{
		TObjectPtr<UUserWidget> Candidate = Bucket.Idle.Pop(/*bAllowShrinking*/ false);
		if (Candidate)
		{
			Widget = Candidate;
			INC_DWORD_STAT(STAT_DP_WidgetPool_Reuse);
		}
	}

	// None idle — construct a fresh instance.
	if (!Widget)
	{
		Widget = CreateNewWidget(Key);
		if (!Widget)
		{
			return nullptr;
		}
	}

	ApplyAcquireReset(Widget);
	Bucket.Live.Add(Widget);
	Bucket.PeakLive = FMath::Max(Bucket.PeakLive, Bucket.Live.Num());

	NotifyAcquired(Widget);
	return Widget;
}

UDP_ViewBase* UDP_WidgetPoolSubsystem::AcquireView(TSubclassOf<UDP_ViewBase> ViewClass, UDP_ViewModelBase* ViewModel,
	APlayerController* OwningPlayer)
{
	if (!ViewClass)
	{
		return nullptr;
	}

	// AcquireWidget accepts the UUserWidget base; the cast back is always valid for a UDP_ViewBase class.
	UUserWidget* Acquired = AcquireWidget(ViewClass, OwningPlayer);
	UDP_ViewBase* View = Cast<UDP_ViewBase>(Acquired);
	if (!View)
	{
		// Defensive: an unexpected null/mismatch — return what we acquired to the pool to avoid a leak.
		if (Acquired)
		{
			ReleaseWidget(Acquired);
		}
		return nullptr;
	}

	View->SetViewModel(ViewModel);
	return View;
}

void UDP_WidgetPoolSubsystem::ReleaseWidget(UUserWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	// Locate the bucket that has this widget live. Linear over buckets is fine: bucket count is
	// small (a handful of classes x local players) and release is not a per-frame hot path.
	for (TPair<FDP_WidgetPoolKey, FDP_WidgetPoolBucket>& Pair : Buckets)
	{
		FDP_WidgetPoolBucket& Bucket = Pair.Value;
		const int32 LiveIndex = Bucket.Live.IndexOfByKey(Widget);
		if (LiveIndex != INDEX_NONE)
		{
			NotifyReturned(Widget);
			ApplyReleaseReset(Widget);
			Bucket.Live.RemoveAtSwap(LiveIndex, 1, /*bAllowShrinking*/ false);
			Bucket.Idle.Add(Widget);
			return;
		}
	}

	// Not tracked by us (already released or never pooled): apply the structural reset anyway so
	// the caller's "release" intent (remove from viewport) is still honoured, and warn.
	ApplyReleaseReset(Widget);
	UE_LOG(LogDPPool, Verbose, TEXT("[WidgetPool] ReleaseWidget on an untracked widget %s (double release?)."),
		*Widget->GetName());
}

void UDP_WidgetPoolSubsystem::WarmPool(TSubclassOf<UUserWidget> WidgetClass, int32 Count, APlayerController* OwningPlayer)
{
	if (!WidgetClass || WidgetClass->HasAnyClassFlags(CLASS_Abstract) || Count <= 0)
	{
		return;
	}

	const FDP_WidgetPoolKey Key = MakeKey(WidgetClass, OwningPlayer);
	FDP_WidgetPoolBucket& Bucket = Buckets.FindOrAdd(Key);

	for (int32 i = 0; i < Count; ++i)
	{
		UUserWidget* Widget = CreateNewWidget(Key);
		if (!Widget)
		{
			break;
		}
		ApplyReleaseReset(Widget);
		Bucket.Idle.Add(Widget);
	}
}

void UDP_WidgetPoolSubsystem::DrainPool(TSubclassOf<UUserWidget> WidgetClass)
{
	if (!WidgetClass)
	{
		return;
	}

	for (TPair<FDP_WidgetPoolKey, FDP_WidgetPoolBucket>& Pair : Buckets)
	{
		if (Pair.Key.WidgetClass != WidgetClass)
		{
			continue;
		}

		FDP_WidgetPoolBucket& Bucket = Pair.Value;
		for (int32 i = Bucket.Idle.Num() - 1; i >= 0; --i)
		{
			UUserWidget* Widget = Bucket.Idle[i];
			if (!Widget || CanReclaim(Widget))
			{
				// Dropping the UPROPERTY ref makes the widget eligible for GC.
				Bucket.Idle.RemoveAt(i, 1, /*bAllowShrinking*/ false);
			}
		}
	}
}

int32 UDP_WidgetPoolSubsystem::GetIdleCount(TSubclassOf<UUserWidget> WidgetClass) const
{
	int32 Total = 0;
	for (const TPair<FDP_WidgetPoolKey, FDP_WidgetPoolBucket>& Pair : Buckets)
	{
		if (Pair.Key.WidgetClass == WidgetClass)
		{
			Total += Pair.Value.Idle.Num();
		}
	}
	return Total;
}

int32 UDP_WidgetPoolSubsystem::GetLiveCount(TSubclassOf<UUserWidget> WidgetClass) const
{
	int32 Total = 0;
	for (const TPair<FDP_WidgetPoolKey, FDP_WidgetPoolBucket>& Pair : Buckets)
	{
		if (Pair.Key.WidgetClass == WidgetClass)
		{
			Total += Pair.Value.Live.Num();
		}
	}
	return Total;
}

void UDP_WidgetPoolSubsystem::ApplyReleaseReset(UUserWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	// Remove from any parent panel / the viewport so a parked widget never renders.
	Widget->RemoveFromParent();

	// Clear the ViewModel so the pooled view does not keep a stale model alive or react to it.
	if (UDP_ViewBase* View = Cast<UDP_ViewBase>(Widget))
	{
		View->SetViewModel(nullptr);
	}

	// Reset cosmetic render state so the next acquirer starts from identity.
	Widget->SetRenderTransform(FWidgetTransform());
	Widget->SetRenderOpacity(1.0f);
	Widget->SetVisibility(ESlateVisibility::Collapsed);
}

void UDP_WidgetPoolSubsystem::ApplyAcquireReset(UUserWidget* Widget)
{
	if (!Widget)
	{
		return;
	}
	Widget->SetRenderTransform(FWidgetTransform());
	Widget->SetRenderOpacity(1.0f);
	// Leave final visibility to the caller (they decide hit-test vs self-hit-test-invisible),
	// but make it visible by default so a freshly-acquired widget shows once placed.
	Widget->SetVisibility(ESlateVisibility::Visible);
}

void UDP_WidgetPoolSubsystem::NotifyAcquired(UUserWidget* Widget)
{
	if (Widget && Widget->Implements<UDP_WidgetPoolable>())
	{
		IDP_WidgetPoolable::Execute_OnAcquiredFromWidgetPool(Widget);
	}
}

void UDP_WidgetPoolSubsystem::NotifyReturned(UUserWidget* Widget)
{
	if (Widget && Widget->Implements<UDP_WidgetPoolable>())
	{
		IDP_WidgetPoolable::Execute_OnReturnedToWidgetPool(Widget);
	}
}

bool UDP_WidgetPoolSubsystem::CanReclaim(const UUserWidget* Widget)
{
	if (Widget && Widget->Implements<UDP_WidgetPoolable>())
	{
		return IDP_WidgetPoolable::Execute_CanWidgetBeReclaimed(Widget);
	}
	return true;
}

void UDP_WidgetPoolSubsystem::PruneDeadOwners()
{
	for (auto It = Buckets.CreateIterator(); It; ++It)
	{
		// A key whose weak OwningPlayer has gone away can never hand its widgets back to the
		// right player; drop the whole bucket so its (idle + live) widgets become GC-eligible.
		if (!It.Key().OwningPlayer.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

FString UDP_WidgetPoolSubsystem::GetDPDebugString_Implementation() const
{
	int32 TotalIdle = 0;
	int32 TotalLive = 0;
	for (const TPair<FDP_WidgetPoolKey, FDP_WidgetPoolBucket>& Pair : Buckets)
	{
		TotalIdle += Pair.Value.Idle.Num();
		TotalLive += Pair.Value.Live.Num();
	}
	return FString::Printf(TEXT("WidgetPool: %d buckets, %d idle, %d live"),
		Buckets.Num(), TotalIdle, TotalLive);
}
