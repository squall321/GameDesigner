// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/Analytics_TelemetryDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Analytics_TelemetryDataAsset"

TArray<float> UAnalytics_TelemetryDataAsset::GetEffectivePerfPercentiles() const
{
	// Validate: non-empty, every value in [0,1], strictly increasing. Any failure falls back to a
	// documented defensive p50/p90/p99 so the summary is never empty or malformed.
	bool bValid = PerfPercentiles.Num() > 0;
	float Previous = -1.f;
	for (const float P : PerfPercentiles)
	{
		if (P < 0.f || P > 1.f || P <= Previous)
		{
			bValid = false;
			break;
		}
		Previous = P;
	}

	if (bValid)
	{
		return PerfPercentiles;
	}

	static const TArray<float> DefensiveDefault = { 0.5f, 0.9f, 0.99f };
	return DefensiveDefault;
}

#if WITH_EDITOR
EDataValidationResult UAnalytics_TelemetryDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (HeatmapBucketSizeUU <= 0.f)
	{
		Context.AddError(LOCTEXT("ZeroBucket", "HeatmapBucketSizeUU must be > 0 (a zero bucket size cannot grid positions)."));
		Result = EDataValidationResult::Invalid;
	}

	if (PerfPercentiles.Num() == 0)
	{
		Context.AddWarning(LOCTEXT("EmptyPercentiles", "PerfPercentiles is empty; a defensive p50/p90/p99 set will be used."));
	}
	else
	{
		float Previous = -1.f;
		for (const float P : PerfPercentiles)
		{
			if (P < 0.f || P > 1.f)
			{
				Context.AddError(LOCTEXT("PercentileRange", "PerfPercentiles values must all lie within [0,1]."));
				Result = EDataValidationResult::Invalid;
				break;
			}
			if (P <= Previous)
			{
				Context.AddError(LOCTEXT("PercentileMonotonic", "PerfPercentiles must be strictly increasing."));
				Result = EDataValidationResult::Invalid;
				break;
			}
			Previous = P;
		}
	}

	if (PerfRingSize <= 0)
	{
		Context.AddError(LOCTEXT("ZeroPerfRing", "PerfRingSize must be >= 1."));
		Result = EDataValidationResult::Invalid;
	}

	if (BreadcrumbRingSize <= 0)
	{
		Context.AddError(LOCTEXT("ZeroBreadcrumbRing", "BreadcrumbRingSize must be >= 1."));
		Result = EDataValidationResult::Invalid;
	}

	if (FunnelCohortBucketCount < 2)
	{
		Context.AddWarning(LOCTEXT("LowCohortBuckets", "FunnelCohortBucketCount < 2 yields a single cohort; a floor of 2 is applied at runtime."));
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
