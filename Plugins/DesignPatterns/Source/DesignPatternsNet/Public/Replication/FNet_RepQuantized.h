// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FNet_RepQuantized.generated.h"

/**
 * Bandwidth-light replicated float.
 *
 * Instead of sending a full 32-bit float, this packs the value into a fixed number of bits over a
 * known [Min, Max] range using a custom NetSerialize. This is the classic "quantize on the wire"
 * trade-off:
 *
 *   PROS  : far fewer bits per update (e.g. health 0..100 in 10 bits instead of 32); great for
 *           values that update frequently and tolerate coarse precision (health bars, charge meters).
 *   CONS  : lossy — the received value is snapped to the nearest quantization step, so it is NOT
 *           suitable for exact financial/seed/hash values or anything compared for exact equality.
 *           Range is fixed at author time; values outside [Min, Max] are clamped on send.
 *
 * Hold the *authoritative* value in Value on the server and let NetSerialize compress it; readers use
 * Get() which already reflects the (possibly de-quantized) replicated value.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_RepFloat
{
	GENERATED_BODY()

	/** The logical value. On the receiving side this is the de-quantized approximation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net")
	float Value = 0.f;

	/** Inclusive lower bound of the quantization range. Values below this are clamped on send. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net")
	float Min = 0.f;

	/** Inclusive upper bound of the quantization range. Values above this are clamped on send. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net")
	float Max = 100.f;

	/** Bits used to encode the quantized value (2..24). More bits = finer steps, more bandwidth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net", meta = (ClampMin = "2", ClampMax = "24"))
	uint8 NumBits = 10;

	FNet_RepFloat() = default;
	FNet_RepFloat(float InValue, float InMin, float InMax, uint8 InNumBits = 10)
		: Value(InValue), Min(InMin), Max(InMax), NumBits(InNumBits) {}

	/** Read the current (de-quantized on clients) value. */
	float Get() const { return Value; }

	/** Set the logical value, clamping into [Min, Max] so it round-trips cleanly through the wire. */
	void Set(float InValue) { Value = FMath::Clamp(InValue, Min, Max); }

	/** Custom quantizing serializer. Returns true; sets bOutSuccess true on success. */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FNet_RepFloat> : public TStructOpsTypeTraitsBase2<FNet_RepFloat>
{
	enum
	{
		WithNetSerializer = true
	};
};

/**
 * Bandwidth-light replicated integer.
 *
 * Sends only NumBits of an unsigned integer biased by Min (so the wire carries Value - Min). Use for
 * small bounded counters — ammo, stacks, lives — where the full 32-bit int would be wasteful.
 *
 *   PROS  : tiny on the wire (e.g. 0..255 in 8 bits); exact within range (NOT lossy, unlike RepFloat).
 *   CONS  : range is fixed at author time; values outside [Min, Min + (1<<NumBits) - 1] are clamped.
 *           Always validate NumBits is wide enough for your max value.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_RepInt
{
	GENERATED_BODY()

	/** The logical integer value (exact within the encodable range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net")
	int32 Value = 0;

	/** Lower bound; the wire carries (Value - Min) as an unsigned field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net")
	int32 Min = 0;

	/** Bits used to encode (Value - Min) (1..31). Encodable max = Min + (1<<NumBits) - 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net", meta = (ClampMin = "1", ClampMax = "31"))
	uint8 NumBits = 8;

	FNet_RepInt() = default;
	FNet_RepInt(int32 InValue, int32 InMin, uint8 InNumBits = 8)
		: Value(InValue), Min(InMin), NumBits(InNumBits) {}

	/** Read the current value. */
	int32 Get() const { return Value; }

	/** Inclusive maximum value encodable with the current Min/NumBits. */
	int32 MaxEncodable() const { return Min + (int32)((1u << NumBits) - 1u); }

	/** Set the logical value, clamping into the encodable range. */
	void Set(int32 InValue) { Value = FMath::Clamp(InValue, Min, MaxEncodable()); }

	/** Custom bit-packed serializer. Returns true; sets bOutSuccess true on success. */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FNet_RepInt> : public TStructOpsTypeTraitsBase2<FNet_RepInt>
{
	enum
	{
		WithNetSerializer = true
	};
};
