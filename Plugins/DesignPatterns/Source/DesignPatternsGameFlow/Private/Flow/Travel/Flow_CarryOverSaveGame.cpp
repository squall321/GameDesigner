// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Flow/Travel/Flow_CarryOverSaveGame.h"

const FFlow_CarryOverRecord* UFlow_CarryOverSaveGame::FindRecord(FGameplayTag Kind) const
{
	if (!Kind.IsValid())
	{
		return nullptr;
	}
	return Records.FindByPredicate([Kind](const FFlow_CarryOverRecord& R) { return R.Kind == Kind; });
}

void UFlow_CarryOverSaveGame::SetRecord(FGameplayTag Kind, const FInstancedStruct& State)
{
	if (!Kind.IsValid())
	{
		return;
	}

	if (FFlow_CarryOverRecord* Existing = Records.FindByPredicate([Kind](const FFlow_CarryOverRecord& R) { return R.Kind == Kind; }))
	{
		Existing->State = State;
		return;
	}

	FFlow_CarryOverRecord NewRecord;
	NewRecord.Kind = Kind;
	NewRecord.State = State;
	Records.Add(MoveTemp(NewRecord));
}

void UFlow_CarryOverSaveGame::ClearRecords()
{
	Records.Reset();
}
