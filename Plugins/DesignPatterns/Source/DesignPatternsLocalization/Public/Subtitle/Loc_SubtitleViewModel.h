// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Subtitle/Loc_SubtitleTypes.h"
#include "Loc_SubtitleViewModel.generated.h"

/**
 * ViewModel the subtitle UI binds to (built on the engine FieldNotification system via UDP_ViewModelBase,
 * NOT the optional MVVM plugin).
 *
 * The subtitle subsystem owns this object and pushes the current visible set + the active accessibility
 * presentation options into it; the ViewModel raises field-changed notifications so any bound view
 * re-reads. It holds NO gameplay pointers and never reaches into the world — it is a pure projection.
 *
 * Observable fields:
 *  - VisibleSubtitles : the ordered lines currently on screen (drives the caption stack).
 *  - VisibleCount     : convenience count for empty-state binding.
 *  - bSubtitlesEnabled: whether captions should render at all (from accessibility options).
 *  - SubtitleSize     : the requested size preset (from accessibility options).
 *  - bBackground      : whether to draw a readability background (from accessibility options).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Localization Subtitle ViewModel"))
class DESIGNPATTERNSLOCALIZATION_API ULoc_SubtitleViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		VisibleSubtitles = 0,
		VisibleCount,
		bSubtitlesEnabled,
		SubtitleSize,
		bBackground,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/**
	 * Replace the visible set (called by the subsystem after every queue change / timer tick). Updates
	 * VisibleSubtitles + VisibleCount and broadcasts. Broadcast is unconditional because per-line
	 * TimeRemaining changes frame-to-frame even when the count is stable.
	 */
	void SetVisibleSubtitles(const TArray<FLoc_ActiveSubtitleView>& InVisible);

	/**
	 * Push the accessibility presentation options (enabled / size / background). Broadcasts only the
	 * fields that actually changed. The subsystem calls this on register and on every option change.
	 */
	void SetAccessibilityPresentation(bool bInEnabled, ESeam_SubtitleSize InSize, bool bInBackground);

	// --- Observable getters ---

	/** The subtitle lines currently on screen, in display order (copied for BP safety). */
	UFUNCTION(BlueprintPure, Category = "Localization|Subtitle")
	TArray<FLoc_ActiveSubtitleView> GetVisibleSubtitles() const { return VisibleSubtitles; }

	/** Number of subtitle lines currently on screen. */
	UFUNCTION(BlueprintPure, Category = "Localization|Subtitle")
	int32 GetVisibleCount() const { return VisibleSubtitles.Num(); }

	/** Whether captions should render at all (mirrors the accessibility option). */
	UFUNCTION(BlueprintPure, Category = "Localization|Subtitle")
	bool AreSubtitlesEnabled() const { return bSubtitlesEnabled; }

	/** The requested subtitle size preset (mirrors the accessibility option). */
	UFUNCTION(BlueprintPure, Category = "Localization|Subtitle")
	ESeam_SubtitleSize GetSubtitleSize() const { return SubtitleSize; }

	/** Whether to draw a readability background behind captions (mirrors the accessibility option). */
	UFUNCTION(BlueprintPure, Category = "Localization|Subtitle")
	bool ShouldDrawBackground() const { return bBackground; }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Backing storage: the visible subtitle lines in display order. */
	UPROPERTY(Transient)
	TArray<FLoc_ActiveSubtitleView> VisibleSubtitles;

	/** Backing storage: whether captions render (from accessibility options; default true). */
	UPROPERTY(Transient)
	bool bSubtitlesEnabled = true;

	/** Backing storage: requested size preset (from accessibility options; default Medium). */
	UPROPERTY(Transient)
	ESeam_SubtitleSize SubtitleSize = ESeam_SubtitleSize::Medium;

	/** Backing storage: readability background flag (from accessibility options; default true). */
	UPROPERTY(Transient)
	bool bBackground = true;
};
