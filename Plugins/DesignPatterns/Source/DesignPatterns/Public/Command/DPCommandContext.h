// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectKey.h"
#include "UObject/SoftObjectPath.h"

// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4 and is merged into
// CoreUObject in 5.5+. Include the right header for the engine band.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "DPCommandContext.generated.h"

/**
 * A stable, replay-survivable reference to a single actor/object that a command operates on.
 *
 * The problem this solves: a raw TObjectPtr / TWeakObjectPtr is invalidated whenever the
 * target actor is destroyed and respawned (level reload, pool recycle, network re-creation),
 * so a recorded command can no longer find what it acted on. We therefore store BOTH:
 *   - FObjectKey:        an O(1) fast path that resolves while the *original* object lives.
 *   - FSoftObjectPath:   a durable path that survives recreation, used as the fallback.
 *
 * Resolve() tries the fast key first, then the soft path, caching the result back into the key.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_CommandTargetHandle
{
	GENERATED_BODY()

	FDP_CommandTargetHandle() = default;

	/** Build a stable handle from a live object. Captures both the fast key and the durable path. */
	explicit FDP_CommandTargetHandle(const UObject* InObject)
	{
		Set(InObject);
	}

	/** (Re)capture both the fast key and the durable soft path from a live object. */
	void Set(const UObject* InObject)
	{
		FastKey = FObjectKey(InObject);
		SoftPath = InObject ? FSoftObjectPath(InObject) : FSoftObjectPath();
	}

	/** True if this handle references anything (even if the target is not currently loaded). */
	bool IsSet() const
	{
		return SoftPath.IsValid() || FastKey != FObjectKey();
	}

	/**
	 * Resolve to a live UObject. Fast path: the cached FObjectKey while the original lives.
	 * Slow path: the durable FSoftObjectPath (handles actor recreation), re-seeding the key.
	 * Returns nullptr if nothing resolves (target not spawned / not loaded).
	 */
	UObject* Resolve() const
	{
		if (UObject* Fast = FastKey.ResolveObjectPtr())
		{
			return Fast;
		}
		if (SoftPath.IsValid())
		{
			if (UObject* ByPath = SoftPath.ResolveObject())
			{
				// Re-seed the fast key so subsequent resolves are O(1) again.
				FastKey = FObjectKey(ByPath);
				return ByPath;
			}
		}
		return nullptr;
	}

	/** The durable string form, useful for logging / DP.Cmd.Dump. */
	FString ToDebugString() const
	{
		return SoftPath.IsValid() ? SoftPath.ToString() : TEXT("<unset>");
	}

	bool operator==(const FDP_CommandTargetHandle& Other) const
	{
		return SoftPath == Other.SoftPath;
	}

private:
	/**
	 * Fast-path identity of the originally-captured object. Mutable so a const Resolve() can
	 * re-seed it after a soft-path resolve. Not a UPROPERTY: FObjectKey is a non-reflected
	 * value type and does NOT keep the object alive (no GC hazard).
	 */
	mutable FObjectKey FastKey;

	/** Durable path that survives actor destruction + recreation. Reflected so it serializes for replay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Command")
	FSoftObjectPath SoftPath;
};

/**
 * The execution context handed to every command on Execute/Undo/CanExecute.
 *
 * Designed to be fully serializable so a recorded command stream can be replayed in a fresh
 * world: targets are stable handles (not raw pointers) and parameters are a wildcard
 * FInstancedStruct so any USTRUCT can be carried without changing this type.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_CommandContext
{
	GENERATED_BODY()

	FDP_CommandContext() = default;

	/** The primary object the command acts on (e.g. the pawn issuing a move). Stable across recreation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Command")
	FDP_CommandTargetHandle Target;

	/**
	 * The originator/instigator of the command (player controller, AI, replay driver). Stable
	 * so an instigator that respawns can still be matched during replay/undo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Command")
	FDP_CommandTargetHandle Instigator;

	/**
	 * Arbitrary, strongly-typed parameters (e.g. a move delta, a target location, an item id).
	 * Wildcard so new command parameter types need zero changes here. Build with the engine's
	 * Make Instanced Struct node in Blueprint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Command")
	FInstancedStruct Params;

	/**
	 * World/replay timestamp (seconds) at which the command was submitted. Used to order the
	 * replay stream and to drive time-accurate playback. 0 until stamped by the history subsystem.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Command")
	double TimeStampSeconds = 0.0;

	/** Convenience: resolve the target as a specific type, or nullptr. */
	template <typename T>
	T* ResolveTarget() const
	{
		return Cast<T>(Target.Resolve());
	}

	/** Convenience: resolve the instigator as a specific type, or nullptr. */
	template <typename T>
	T* ResolveInstigator() const
	{
		return Cast<T>(Instigator.Resolve());
	}
};
