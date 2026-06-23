// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pacing/Lvl_TensionCurveDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Lvl_TensionCurveDataAsset"

namespace
{
	/** Defensive minimum encounter duration so normalized time math never divides by zero. */
	constexpr float GMinDuration = 0.1f;

	/** Inert-default tension when no curve is authored. */
	constexpr float GDefaultTension = 0.5f;
}

ULvl_TensionCurveDataAsset::ULvl_TensionCurveDataAsset()
{
}

float ULvl_TensionCurveDataAsset::SampleTension(float NormalizedTime01) const
{
	const FRichCurve* Curve = TensionOverTime.GetRichCurveConst();
	if (!Curve || Curve->GetNumKeys() == 0)
	{
		return GDefaultTension; // documented inert default
	}
	const float T = FMath::Clamp(NormalizedTime01, 0.f, 1.f);
	return FMath::Clamp(Curve->Eval(T, GDefaultTension), 0.f, 1.f);
}

void ULvl_TensionCurveDataAsset::GetClampedThresholds(float& OutRelax, float& OutEscalate) const
{
	OutRelax = FMath::Clamp(RelaxThreshold, 0.f, 1.f);
	OutEscalate = FMath::Clamp(EscalateThreshold, 0.f, 1.f);
	// Guarantee escalate strictly above relax (defensive — keeps the hysteresis band non-empty).
	if (OutEscalate <= OutRelax)
	{
		OutEscalate = FMath::Min(1.f, OutRelax + KINDA_SMALL_NUMBER);
	}
}

float ULvl_TensionCurveDataAsset::TensionToProgressionInput(float Tension) const
{
	float Relax, Escalate;
	GetClampedThresholds(Relax, Escalate);
	const float T = FMath::Clamp(Tension, 0.f, 1.f);

	const float RelaxedP = FMath::Clamp(RelaxedProgressionInput, 0.f, 1.f);
	const float EscalatedP = FMath::Clamp(EscalatedProgressionInput, 0.f, 1.f);

	if (T <= Relax)
	{
		return RelaxedP;
	}
	if (T >= Escalate)
	{
		return EscalatedP;
	}
	// Linear blend across the hysteresis band so a producer ignoring bands still gets a sane value.
	const float Alpha = (T - Relax) / FMath::Max(KINDA_SMALL_NUMBER, Escalate - Relax);
	return FMath::Lerp(RelaxedP, EscalatedP, Alpha);
}

float ULvl_TensionCurveDataAsset::GetEffectiveDuration() const
{
	return FMath::Max(GMinDuration, EncounterDuration);
}

#if WITH_EDITOR
EDataValidationResult ULvl_TensionCurveDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (EncounterDuration <= 0.f)
	{
		Context.AddError(LOCTEXT("BadDuration", "EncounterDuration must be > 0."));
		Result = EDataValidationResult::Invalid;
	}
	if (EscalateThreshold <= RelaxThreshold)
	{
		Context.AddError(LOCTEXT("BadThresholds", "EscalateThreshold must be > RelaxThreshold (hysteresis band)."));
		Result = EDataValidationResult::Invalid;
	}
	const FRichCurve* Curve = TensionOverTime.GetRichCurveConst();
	if (!Curve || Curve->GetNumKeys() == 0)
	{
		Context.AddWarning(LOCTEXT("NoCurve", "TensionOverTime has no keys; tension defaults to a constant 0.5."));
	}
	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
