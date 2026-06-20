// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Spawn/AI_EncounterDataAsset.h"
#include "Spawn/AI_WaveDataAsset.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

int32 UAI_EncounterDataAsset::SampleBudget(float ProgressionInput) const
{
	// A curve with no keys yields a neutral multiplier of 1 (documented fallback), so the budget is
	// exactly BaseBudget. We read the const FRichCurve via the RuntimeFloatCurve's external/internal curve.
	const FRichCurve* Curve = BudgetByProgression.GetRichCurveConst();
	const float Multiplier = (Curve && Curve->GetNumKeys() > 0) ? Curve->Eval(ProgressionInput, 1.f) : 1.f;
	const int32 Result = FMath::RoundToInt(static_cast<float>(BaseBudget) * Multiplier);
	return FMath::Max(1, Result);
}

float UAI_EncounterDataAsset::SampleDifficulty(float ProgressionInput) const
{
	const FRichCurve* Curve = DifficultyByProgression.GetRichCurveConst();
	const float Value = (Curve && Curve->GetNumKeys() > 0) ? Curve->Eval(ProgressionInput, 1.f) : 1.f;
	return FMath::Max(0.f, Value);
}

FGameplayTag UAI_EncounterDataAsset::GetEffectiveEncounterTag() const
{
	return EncounterTag.IsValid() ? EncounterTag : DataTag;
}

FName UAI_EncounterDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("AI_Encounter"));
}

#if WITH_EDITOR
EDataValidationResult UAI_EncounterDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Waves.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("AI Encounter asset has no waves.")));
		Result = EDataValidationResult::Invalid;
	}

	for (int32 Index = 0; Index < Waves.Num(); ++Index)
	{
		if (Waves[Index].IsNull())
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("AI Encounter wave slot %d is empty (null soft pointer)."), Index)));
		}
	}

	for (int32 Index = 0; Index < GateConditions.Num(); ++Index)
	{
		if (!GateConditions[Index].HubFlagKey.IsValid())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("AI Encounter gate condition %d has no HubFlagKey."), Index)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif
