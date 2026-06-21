// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Results/GM_ScoreboardViewModel.h"

#include "DesignPatternsGameModeModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "UObject/ScriptInterface.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UGM_ScoreboardViewModel's observable fields by name (order matches EField). */
	struct FGM_ScoreboardViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UGM_ScoreboardViewModel::EField::Num];

		static FFieldId MakeId(UGM_ScoreboardViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UGM_ScoreboardViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FGM_ScoreboardViewModelDescriptor::FieldNames[(int32)UGM_ScoreboardViewModel::EField::Num] =
	{
		FName(TEXT("Rows")),
		FName(TEXT("bResultsFinal")),
	};

	static const FGM_ScoreboardViewModelDescriptor GGM_ScoreboardViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UGM_ScoreboardViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GGM_ScoreboardViewModelDescriptor;
}

UE::FieldNotification::FFieldId UGM_ScoreboardViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FGM_ScoreboardViewModelDescriptor::MakeId(Field);
}

void UGM_ScoreboardViewModel::Refresh(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return;
	}

	// Resolve the score source through the locator. Unresolved -> documented inert default: empty,
	// non-final scoreboard (so the screen renders empty instead of asserting).
	TArray<FGM_ScoreboardRowView> NewRows;
	bool bNewFinal = false;

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContextObject);

	UObject* SourceObj = Locator ? Locator->ResolveService(GameModeNativeTags::Service_GM_Score) : nullptr;
	if (SourceObj && SourceObj->Implements<USeam_ScoreSource>())
	{
		TArray<FSeam_ScoreRow> SeamRows;
		ISeam_ScoreSource::Execute_GetAllScores(SourceObj, SeamRows);
		bNewFinal = ISeam_ScoreSource::Execute_AreResultsFinal(SourceObj);

		// Sort descending by score; stable so equal scores keep source order.
		SeamRows.StableSort([](const FSeam_ScoreRow& A, const FSeam_ScoreRow& B)
		{
			return A.Score > B.Score;
		});

		NewRows.Reserve(SeamRows.Num());
		int32 Rank = 0;
		for (const FSeam_ScoreRow& Src : SeamRows)
		{
			FGM_ScoreboardRowView View;
			View.Rank = ++Rank;
			View.Key = Src.Key;
			View.DisplayName = Src.DisplayName;
			View.Score = Src.Score;
			NewRows.Add(MoveTemp(View));
		}
	}
	else
	{
		UE_LOG(LogDP, Verbose,
			TEXT("GM_ScoreboardViewModel::Refresh: no ISeam_ScoreSource registered — empty board."));
	}

	// Manual change detection (avoids relying on FText/struct operator== semantics): broadcast Rows only
	// when the projected set actually differs.
	bool bRowsChanged = (NewRows.Num() != Rows.Num());
	if (!bRowsChanged)
	{
		for (int32 Index = 0; Index < NewRows.Num(); ++Index)
		{
			const FGM_ScoreboardRowView& A = NewRows[Index];
			const FGM_ScoreboardRowView& B = Rows[Index];
			// Compare display labels by source string (stable across engine versions; lossless for the
			// "did the visible label change?" question a scoreboard needs).
			if (A.Rank != B.Rank ||
				A.Key != B.Key ||
				A.Score != B.Score ||
				!A.DisplayName.ToString().Equals(B.DisplayName.ToString(), ESearchCase::CaseSensitive))
			{
				bRowsChanged = true;
				break;
			}
		}
	}

	if (bRowsChanged)
	{
		Rows = MoveTemp(NewRows);
		BroadcastFieldValueChanged(GetFieldId(EField::Rows));
	}

	// bool supports operator!= so SetProperty's store-and-notify is appropriate here.
	SetProperty(GetFieldId(EField::bResultsFinal), bResultsFinal, bNewFinal);
}
