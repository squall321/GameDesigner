// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Placement/Lvl_BiomeMaskComponent.h"
#include "Curves/CurveFloat.h"

ULvl_BiomeMaskComponent::ULvl_BiomeMaskComponent()
{
	// Pure read helper: no ticking, never replicated.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

float ULvl_BiomeMaskComponent::LatticeHash(int32 IX, int32 IY) const
{
	// A stable integer hash over (seed, ix, iy) folded into [0,1). Deterministic across machines.
	uint32 H = static_cast<uint32>(NoiseSeed) * 2654435761u;
	H ^= static_cast<uint32>(IX) * 2246822519u;
	H = (H << 13) | (H >> 19);
	H ^= static_cast<uint32>(IY) * 3266489917u;
	H *= 668265263u;
	H ^= (H >> 15);
	return static_cast<float>(H & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float ULvl_BiomeMaskComponent::ValueNoise2D(float X, float Y) const
{
	// Bilinear value noise on the integer lattice (smoothstep-interpolated).
	const float FX = FMath::Floor(X);
	const float FY = FMath::Floor(Y);
	const int32 IX = static_cast<int32>(FX);
	const int32 IY = static_cast<int32>(FY);

	const float TX = X - FX;
	const float TY = Y - FY;
	const float SX = TX * TX * (3.f - 2.f * TX); // smoothstep
	const float SY = TY * TY * (3.f - 2.f * TY);

	const float V00 = LatticeHash(IX, IY);
	const float V10 = LatticeHash(IX + 1, IY);
	const float V01 = LatticeHash(IX, IY + 1);
	const float V11 = LatticeHash(IX + 1, IY + 1);

	const float A = FMath::Lerp(V00, V10, SX);
	const float B = FMath::Lerp(V01, V11, SX);
	return FMath::Clamp(FMath::Lerp(A, B, SY), 0.f, 1.f);
}

float ULvl_BiomeMaskComponent::SampleNoise01(const FVector& WorldLoc) const
{
	const float Freq = FMath::Max(0.000001f, NoiseFrequency);
	float Raw = ValueNoise2D(static_cast<float>(WorldLoc.X) * Freq, static_cast<float>(WorldLoc.Y) * Freq);

	if (BiomeNoiseProfile)
	{
		Raw = FMath::Clamp(BiomeNoiseProfile->GetFloatValue(Raw), 0.f, 1.f);
	}
	return Raw;
}

FGameplayTag ULvl_BiomeMaskComponent::GetBiomeAt(const FVector& WorldLoc, float& OutWeight) const
{
	OutWeight = 0.f;
	const float Value = SampleNoise01(WorldLoc);

	for (const FLvl_BiomeBand& Band : BiomeBands)
	{
		const float Lo = FMath::Min(Band.MinValue, Band.MaxValue);
		const float Hi = FMath::Max(Band.MinValue, Band.MaxValue);
		if (Value >= Lo && Value < Hi)
		{
			// Membership weight: 1.0 at the band centre, falling toward 0 at the edges.
			const float Width = FMath::Max(KINDA_SMALL_NUMBER, Hi - Lo);
			const float Centre = (Lo + Hi) * 0.5f;
			OutWeight = 1.f - (FMath::Abs(Value - Centre) / (Width * 0.5f));
			OutWeight = FMath::Clamp(OutWeight, 0.f, 1.f);
			return Band.BiomeTag;
		}
	}
	return FGameplayTag();
}
