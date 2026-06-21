// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Score/Seam_ScoreSource.h"
#include "GM_ScoreboardViewModel.generated.h"

/**
 * One scoreboard row as the results view consumes it: the source FSeam_ScoreRow plus a derived 1-based
 * rank for display. Flat and view-ready — no gameplay or object refs.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_ScoreboardRowView
{
	GENERATED_BODY()

	/** 1-based placement after sorting (1 = leader). Ties share neither rank nor order beyond sort stability. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Results")
	int32 Rank = 0;

	/** Row key (a team tag, a player projected to a tag, or a category). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Results")
	FGameplayTag Key;

	/** Display label for the row (from the score source). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Results")
	FText DisplayName;

	/** The score value for this row. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Results")
	int64 Score = 0;
};

/**
 * ViewModel for a scoreboard / results screen.
 *
 * It reads the match scores through the ISeam_ScoreSource seam (resolved by stable service tag from the
 * service locator) — never from any concrete score subsystem — so the results UI stays decoupled from
 * the GameMode module. On Refresh it snapshots all rows, sorts them descending by score, assigns display
 * ranks, mirrors the seam's "results final" flag, and writes the two observable FieldNotify fields (Rows,
 * bResultsFinal). Any bound UDP_ViewBase re-reads them on change.
 *
 * The ViewModel is local/cosmetic and holds NO gameplay pointers and NO world ref of its own; callers
 * push a world-context object into Refresh. When the score seam is unresolved it degrades to a documented
 * inert default: an empty, non-final scoreboard (so the screen renders empty rather than asserting).
 */
UCLASS(BlueprintType, meta = (DisplayName = "GM Scoreboard ViewModel"))
class DESIGNPATTERNSGAMEMODE_API UGM_ScoreboardViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this ViewModel's observable fields (must match the descriptor order). */
	enum class EField : int32
	{
		/** The sorted, ranked, view-ready rows. */
		Rows = 0,
		/** True once the match results are final. */
		bResultsFinal,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this ViewModel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/**
	 * Re-read the score source, sort + rank rows, and broadcast the Rows / bResultsFinal field changes
	 * if they actually differ. Resolves the ISeam_ScoreSource from the locator using the supplied
	 * world-context object (e.g. the owning widget), so the ViewModel needs no world pointer of its own.
	 *
	 * @param WorldContextObject  Any object with a world (e.g. the owning view widget). No-op if null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Results")
	void Refresh(UObject* WorldContextObject);

	/**
	 * The sorted, ranked rows (observable field EField::Rows). Returned by value (UHT forbids returning a
	 * container by const ref from a UFUNCTION). Views bind to the field-changed multicast and re-read this.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Results")
	TArray<FGM_ScoreboardRowView> GetRows() const { return Rows; }

	/** True once the match is over and results are final (observable field EField::bResultsFinal). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Results")
	bool AreResultsFinal() const { return bResultsFinal; }

private:
	/** Sorted, ranked, view-ready rows (observable field EField::Rows). */
	UPROPERTY(Transient, Category = "DesignPatterns|GameMode|Results", meta = (AllowPrivateAccess = "true"))
	TArray<FGM_ScoreboardRowView> Rows;

	/** Whether results are final, mirrored from the seam (observable field EField::bResultsFinal). */
	UPROPERTY(Transient, Category = "DesignPatterns|GameMode|Results", meta = (AllowPrivateAccess = "true"))
	bool bResultsFinal = false;
};
