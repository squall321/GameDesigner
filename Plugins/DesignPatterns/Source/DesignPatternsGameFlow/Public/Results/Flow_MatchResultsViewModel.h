// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Score/Seam_ScoreSource.h"
#include "Flow_MatchResultsViewModel.generated.h"

/**
 * ViewModel the post-match results / scoreboard UI binds to (built on the engine FieldNotification
 * system via UDP_ViewModelBase, NOT the optional MVVM plugin).
 *
 * It reads the match scores through the shared ISeam_ScoreSource (resolved from the service locator),
 * so it does NOT depend on the GameMode/score concrete type and reads correctly on clients (the score
 * source is a replicated GameState-owned carrier). The Refresh call re-reads the seam and republishes
 * the rows; the flow subsystem (or the results screen) calls it on entering the Results phase.
 *
 * The ViewModel holds NO gameplay pointers — it copies flat FSeam_ScoreRow values out of the seam.
 *
 * Observable fields:
 *  - Rows           : the ordered scoreboard rows (sorted descending by score).
 *  - bResultsFinal  : whether the match is over and the results are final (gates a "continue" button).
 *  - RowCount       : convenience count for empty-state binding.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Flow Match Results ViewModel"))
class DESIGNPATTERNSGAMEFLOW_API UFlow_MatchResultsViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		Rows = 0,
		bResultsFinal,
		RowCount,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/**
	 * Re-read scores from the ISeam_ScoreSource registered under ScoreSourceServiceKey and republish the
	 * sorted rows + the results-final flag. Safe to call with no provider (clears to an empty board).
	 * WorldContext is any object with a world (used to resolve the GameInstance service locator).
	 */
	UFUNCTION(BlueprintCallable, Category = "Flow|Results", meta = (WorldContext = "WorldContextObject"))
	void RefreshFromSeam(UObject* WorldContextObject, FGameplayTag ScoreSourceServiceKey);

	/**
	 * Push rows + final flag directly (for tests, or when a caller already holds the score source). The
	 * rows are sorted descending by score before publishing.
	 */
	void SetResults(const TArray<FSeam_ScoreRow>& InRows, bool bInResultsFinal);

	// --- Observable getters ---

	/** The scoreboard rows in display order (sorted descending by score; copied for BP safety). */
	UFUNCTION(BlueprintPure, Category = "Flow|Results")
	TArray<FSeam_ScoreRow> GetRows() const { return Rows; }

	/** True once the match is over and the results are final. */
	UFUNCTION(BlueprintPure, Category = "Flow|Results")
	bool AreResultsFinal() const { return bResultsFinal; }

	/** Number of scoreboard rows. */
	UFUNCTION(BlueprintPure, Category = "Flow|Results")
	int32 GetRowCount() const { return Rows.Num(); }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Backing storage: scoreboard rows, sorted descending by score. */
	UPROPERTY(Transient)
	TArray<FSeam_ScoreRow> Rows;

	/** Backing storage: results-final flag. */
	UPROPERTY(Transient)
	bool bResultsFinal = false;
};
