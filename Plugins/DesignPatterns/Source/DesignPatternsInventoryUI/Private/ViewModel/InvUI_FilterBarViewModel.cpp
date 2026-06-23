// Copyright DesignPatterns plugin. All Rights Reserved.

#include "ViewModel/InvUI_FilterBarViewModel.h"
#include "ViewModel/InvUI_GridViewModel.h"
#include "Strategy/InvUI_SortStrategy.h"
#include "Strategy/InvUI_SearchSortStrategy.h"
#include "Settings/InvUI_Settings.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/** Hand-rolled descriptor enumerating UInvUI_FilterBarViewModel's observable fields. */
	struct FInvUI_FilterBarViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UInvUI_FilterBarViewModel::EField::Num];

		static FFieldId MakeId(UInvUI_FilterBarViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UInvUI_FilterBarViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FInvUI_FilterBarViewModelDescriptor::FieldNames[(int32)UInvUI_FilterBarViewModel::EField::Num] =
	{
		FName(TEXT("SearchText")),
		FName(TEXT("ActiveTypeFilter")),
		FName(TEXT("ActiveSortMode")),
		FName(TEXT("bShowEmpty")),
	};

	static const FInvUI_FilterBarViewModelDescriptor GInvUI_FilterBarViewModelDescriptor;
}

UInvUI_FilterBarViewModel::UInvUI_FilterBarViewModel()
{
}

const UE::FieldNotification::IClassDescriptor& UInvUI_FilterBarViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GInvUI_FilterBarViewModelDescriptor;
}

UE::FieldNotification::FFieldId UInvUI_FilterBarViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FInvUI_FilterBarViewModelDescriptor::MakeId(Field);
}

void UInvUI_FilterBarViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UInvUI_FilterBarViewModel::BindGrid(UInvUI_GridViewModel* InGridViewModel, FGameplayTag InKindTag)
{
	GridViewModel = InGridViewModel;
	KindTag = InKindTag;

	if (SearchSort == nullptr)
	{
		SearchSort = NewObject<UInvUI_SortBySearchRelevance>(this);
	}

	LoadPreference(KindTag);
}

void UInvUI_FilterBarViewModel::RegisterSortMode(FGameplayTag SortModeTag, UInvUI_SortStrategy* Strategy)
{
	if (SortModeTag.IsValid() && Strategy != nullptr)
	{
		SortStrategies.Add(SortModeTag, Strategy);
	}
}

void UInvUI_FilterBarViewModel::SetSearchText(FText InSearchText)
{
	if (SearchText.EqualTo(InSearchText))
	{
		return;
	}
	SearchText = InSearchText;
	BroadcastField(EField::SearchText);

	if (SearchSort != nullptr)
	{
		SearchSort->SetSearchTerm(InSearchText.ToString());
	}

	// When a non-empty search is active, the relevance sort drives ordering; when cleared, fall
	// back to whatever the active sort mode selected.
	ApplySortToGrid();
}

void UInvUI_FilterBarViewModel::SetTypeFilter(const FGameplayTagContainer& InFilter)
{
	if (ActiveTypeFilter == InFilter)
	{
		return;
	}
	ActiveTypeFilter = InFilter;
	BroadcastField(EField::ActiveTypeFilter);

	if (GridViewModel)
	{
		GridViewModel->SetItemFilter(ActiveTypeFilter); // rebuilds the grid
	}
}

void UInvUI_FilterBarViewModel::SetSortMode(FGameplayTag InSortMode)
{
	if (SetProperty(GetFieldId(EField::ActiveSortMode), ActiveSortMode, InSortMode))
	{
		ApplySortToGrid();
		SavePreference();
	}
}

void UInvUI_FilterBarViewModel::SetShowEmpty(bool bInShowEmpty)
{
	if (SetProperty(GetFieldId(EField::bShowEmpty), bShowEmpty, bInShowEmpty))
	{
		if (GridViewModel)
		{
			GridViewModel->SetShowEmptySlots(bShowEmpty); // rebuilds the grid
		}
		SavePreference();
	}
}

void UInvUI_FilterBarViewModel::ApplySortToGrid()
{
	if (GridViewModel == nullptr)
	{
		return;
	}

	// A live search term takes precedence: relevance-rank by name regardless of the chosen mode.
	if (SearchSort != nullptr && !SearchText.IsEmpty())
	{
		GridViewModel->SetSortStrategy(SearchSort);
		return;
	}

	// Otherwise install the strategy registered for the active sort mode (null = container order).
	if (TObjectPtr<UInvUI_SortStrategy>* Found = SortStrategies.Find(ActiveSortMode))
	{
		GridViewModel->SetSortStrategy(*Found);
	}
	else
	{
		GridViewModel->SetSortStrategy(nullptr);
	}
}

void UInvUI_FilterBarViewModel::SavePreference()
{
	if (!KindTag.IsValid())
	{
		return;
	}
	// Write into the settings CDO (config) and persist. Per-machine UI preference, not gameplay state.
	UInvUI_Settings* Settings = GetMutableDefault<UInvUI_Settings>();
	if (Settings == nullptr)
	{
		return;
	}
	Settings->DefaultSortModeByKind.Add(KindTag, ActiveSortMode);
	Settings->ShowEmptySlotsByKind.Add(KindTag, bShowEmpty);
	Settings->SaveConfig();
}

void UInvUI_FilterBarViewModel::LoadPreference(FGameplayTag InKindTag)
{
	KindTag = InKindTag;

	const UInvUI_Settings* Settings = UInvUI_Settings::Get();
	const FGameplayTag SavedSort = Settings ? Settings->GetSortModeForKind(KindTag) : FGameplayTag();
	const bool bSavedShowEmpty = Settings ? Settings->GetShowEmptyForKind(KindTag, bShowEmpty) : bShowEmpty;

	SetProperty(GetFieldId(EField::ActiveSortMode), ActiveSortMode, SavedSort);
	SetProperty(GetFieldId(EField::bShowEmpty), bShowEmpty, bSavedShowEmpty);

	if (GridViewModel)
	{
		GridViewModel->SetShowEmptySlots(bShowEmpty);
		ApplySortToGrid();
	}
}
