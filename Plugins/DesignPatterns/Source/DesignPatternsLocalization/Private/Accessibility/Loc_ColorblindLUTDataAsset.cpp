// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Accessibility/Loc_ColorblindLUTDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FLinearColor ULoc_ColorblindLUTDataAsset::Sample(ESeam_ColorblindMode Mode, const FLinearColor& In) const
{
	const FLoc_ColorblindCube* Cube = Cubes.Find(Mode);
	if (!Cube)
	{
		return In; // no cube for this mode -> identity.
	}

	const int32 N = Cube->Resolution;
	if (N < 2 || Cube->Cells.Num() != N * N * N)
	{
		// Inconsistent / empty cube -> identity (documented inert default).
		return In;
	}

	// Nearest-cell sampling. Clamp the input to [0,1] per channel, quantize to the cube grid, and look up.
	auto QuantizeAxis = [N](float V) -> int32
	{
		const float Clamped = FMath::Clamp(V, 0.f, 1.f);
		return FMath::Clamp(FMath::RoundToInt(Clamped * (N - 1)), 0, N - 1);
	};

	const int32 R = QuantizeAxis(In.R);
	const int32 G = QuantizeAxis(In.G);
	const int32 B = QuantizeAxis(In.B);

	// R-major, then G, then B.
	const int32 Index = (R * N * N) + (G * N) + B;
	FLinearColor Out = Cube->Cells[Index];

	// Preserve the input alpha (the cube corrects hue, not transparency).
	Out.A = In.A;
	return Out;
}

FName ULoc_ColorblindLUTDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Loc_ColorblindLUT"));
}

#if WITH_EDITOR
EDataValidationResult ULoc_ColorblindLUTDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (const TPair<ESeam_ColorblindMode, FLoc_ColorblindCube>& Pair : Cubes)
	{
		const FLoc_ColorblindCube& Cube = Pair.Value;
		if (Cube.Resolution >= 2)
		{
			const int32 Expected = Cube.Resolution * Cube.Resolution * Cube.Resolution;
			if (Cube.Cells.Num() != Expected)
			{
				Context.AddError(FText::FromString(
					FString::Printf(TEXT("Loc_ColorblindLUT: a cube has Resolution=%d (expects %d cells) but has %d; it will sample to identity."),
						Cube.Resolution, Expected, Cube.Cells.Num())));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	return Result;
}
#endif
