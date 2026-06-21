// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Results/Flow_MatchResultsViewModel.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UFlow_MatchResultsViewModel's observable fields by name. */
	struct FFlow_MatchResultsViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UFlow_MatchResultsViewModel::EField::Num];

		static FFieldId MakeId(UFlow_MatchResultsViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UFlow_MatchResultsViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FFlow_MatchResultsViewModelDescriptor::FieldNames[(int32)UFlow_MatchResultsViewModel::EField::Num] =
	{
		FName(TEXT("Rows")),
		FName(TEXT("bResultsFinal")),
		FName(TEXT("RowCount")),
	};

	static const FFlow_MatchResultsViewModelDescriptor GFlow_MatchResultsViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UFlow_MatchResultsViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GFlow_MatchResultsViewModelDescriptor;
}

UE::FieldNotification::FFieldId UFlow_MatchResultsViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FFlow_MatchResultsViewModelDescriptor::MakeId(Field);
}

void UFlow_MatchResultsViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UFlow_MatchResultsViewModel::RefreshFromSeam(UObject* WorldContextObject, FGameplayTag ScoreSourceServiceKey)
{
	TArray<FSeam_ScoreRow> NewRows;
	bool bFinal = false;

	UDP_ServiceLocatorSubsystem* Locator = WorldContextObject
		? FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContextObject)
		: nullptr;

	UObject* ScoreObj = (Locator && ScoreSourceServiceKey.IsValid())
		? Locator->ResolveService(ScoreSourceServiceKey)
		: nullptr;

	if (ScoreObj && ScoreObj->Implements<USeam_ScoreSource>())
	{
		ISeam_ScoreSource::Execute_GetAllScores(ScoreObj, NewRows);
		bFinal = ISeam_ScoreSource::Execute_AreResultsFinal(ScoreObj);
	}
	else
	{
		// Documented inert default: no score source resolved => an empty, non-final board. The results
		// screen then shows its empty state rather than crashing.
		UE_LOG(LogDP, Verbose, TEXT("[Results] No ISeam_ScoreSource under %s; showing empty results."),
			*ScoreSourceServiceKey.ToString());
	}

	SetResults(NewRows, bFinal);
}

void UFlow_MatchResultsViewModel::SetResults(const TArray<FSeam_ScoreRow>& InRows, bool bInResultsFinal)
{
	const int32 PreviousCount = Rows.Num();

	// Sort descending by score so the winner is row 0. Stable on equal scores (preserve seam order).
	Rows = InRows;
	Rows.StableSort([](const FSeam_ScoreRow& A, const FSeam_ScoreRow& B)
	{
		return A.Score > B.Score;
	});

	// The rows always replace wholesale; broadcast unconditionally since the contents can change even
	// when the count is stable (scores updated).
	BroadcastField(EField::Rows);

	if (PreviousCount != Rows.Num())
	{
		BroadcastField(EField::RowCount);
	}

	if (bResultsFinal != bInResultsFinal)
	{
		bResultsFinal = bInResultsFinal;
		BroadcastField(EField::bResultsFinal);
	}
}
