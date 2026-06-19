// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Replication/FNet_RepQuantized.h"

bool FNet_RepFloat::NetSerialize(FArchive& Ar, UPackageMap* /*Map*/, bool& bOutSuccess)
{
	// Defensive: keep params sane so the quantization math never divides by zero or overflows.
	const uint8 Bits = (uint8)FMath::Clamp<int32>(NumBits, 2, 24);
	const float SafeMin = Min;
	const float SafeMax = (Max > Min) ? Max : (Min + 1.f);
	const uint32 MaxQuant = (1u << Bits) - 1u;

	if (Ar.IsSaving())
	{
		const float Clamped = FMath::Clamp(Value, SafeMin, SafeMax);
		const float Alpha = (Clamped - SafeMin) / (SafeMax - SafeMin); // 0..1
		uint32 Quant = (uint32)FMath::RoundToInt(Alpha * (float)MaxQuant);
		Quant = FMath::Min(Quant, MaxQuant);
		Ar.SerializeBits(&Quant, Bits);
	}
	else
	{
		uint32 Quant = 0;
		Ar.SerializeBits(&Quant, Bits);
		const float Alpha = (MaxQuant > 0) ? ((float)Quant / (float)MaxQuant) : 0.f;
		Value = SafeMin + Alpha * (SafeMax - SafeMin);
	}

	bOutSuccess = true;
	return true;
}

bool FNet_RepInt::NetSerialize(FArchive& Ar, UPackageMap* /*Map*/, bool& bOutSuccess)
{
	const uint8 Bits = (uint8)FMath::Clamp<int32>(NumBits, 1, 31);
	const uint32 MaxField = (1u << Bits) - 1u;

	if (Ar.IsSaving())
	{
		// Bias by Min and clamp into the encodable unsigned range before sending.
		const int64 Biased = (int64)Value - (int64)Min;
		const uint32 Field = (uint32)FMath::Clamp<int64>(Biased, 0, (int64)MaxField);
		uint32 Mutable = Field;
		Ar.SerializeBits(&Mutable, Bits);
	}
	else
	{
		uint32 Field = 0;
		Ar.SerializeBits(&Field, Bits);
		Value = Min + (int32)Field;
	}

	bOutSuccess = true;
	return true;
}
