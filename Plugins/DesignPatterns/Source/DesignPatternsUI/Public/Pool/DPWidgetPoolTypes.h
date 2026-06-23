// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DPWidgetPoolTypes.generated.h"

class UUserWidget;
class APlayerController;

/**
 * Composite key identifying one widget pool bucket: a (widget class, owning player) pair.
 *
 * Pooled UUserWidgets are owned by a specific APlayerController (their OwningPlayer drives input
 * routing, viewport and HUD ownership), so a single class can have a distinct pool per local
 * player. Keying by both keeps split-screen players' recycled widgets from leaking across to the
 * wrong viewport.
 *
 * The owning player is stored WEAKLY here purely for hashing/equality identity; the subsystem
 * prunes buckets whose player has gone away, so a dangling weak pointer never resurrects a widget.
 */
USTRUCT()
struct DESIGNPATTERNSUI_API FDP_WidgetPoolKey
{
	GENERATED_BODY()

	/** The pooled widget class. */
	UPROPERTY()
	TSubclassOf<UUserWidget> WidgetClass = nullptr;

	/** The owning local player controller (weak — identity only; the subsystem prunes dead players). */
	UPROPERTY()
	TWeakObjectPtr<APlayerController> OwningPlayer;

	FDP_WidgetPoolKey() = default;
	FDP_WidgetPoolKey(TSubclassOf<UUserWidget> InClass, APlayerController* InPlayer)
		: WidgetClass(InClass), OwningPlayer(InPlayer)
	{
	}

	bool operator==(const FDP_WidgetPoolKey& Other) const
	{
		return WidgetClass == Other.WidgetClass && OwningPlayer == Other.OwningPlayer;
	}

	friend uint32 GetTypeHash(const FDP_WidgetPoolKey& Key)
	{
		return HashCombine(GetTypeHash(Key.WidgetClass), GetTypeHash(Key.OwningPlayer));
	}
};

/**
 * One pool bucket's state: idle (ready) and live (checked-out) widget instances for a single key.
 * Held by value in the subsystem's map. Both arrays are UPROPERTY so the widgets stay GC-rooted
 * while pooled or live.
 */
USTRUCT()
struct DESIGNPATTERNSUI_API FDP_WidgetPoolBucket
{
	GENERATED_BODY()

	/** Idle, ready-to-hand-out widget instances. */
	UPROPERTY()
	TArray<TObjectPtr<UUserWidget>> Idle;

	/** Currently checked-out widget instances. */
	UPROPERTY()
	TArray<TObjectPtr<UUserWidget>> Live;

	/** Highest simultaneous live count observed, for stats/debug. */
	UPROPERTY()
	int32 PeakLive = 0;
};
