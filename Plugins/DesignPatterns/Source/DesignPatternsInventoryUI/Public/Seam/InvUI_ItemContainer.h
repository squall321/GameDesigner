// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "InvUI_ItemContainer.generated.h"

/**
 * Stable, net-/save-safe identity of a *container instance* the UI can bind to.
 *
 * A window never binds to a backend by raw object pointer (not replicable, not save-stable)
 * nor by an array index into some slot list (indices renumber). Instead it binds by this
 * pair: a KindTag describing what the container *is* (e.g. a designer "player bag",
 * "world chest", "vendor stock" tag) and an Instance GUID disambiguating *which* one. The
 * registry maps this id to the live IInvUI_ItemContainer; the server re-derives the real
 * backend from this id when validating an intent, so a client can never name an arbitrary
 * target by pointer.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_ContainerInstanceId
{
	GENERATED_BODY()

	/** What kind of container this is (designer-authored, opaque to this module). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "InvUI|Container")
	FGameplayTag KindTag;

	/** Which specific instance of that kind. Invalid (all-zero) until assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "InvUI|Container")
	FGuid Instance;

	FInvUI_ContainerInstanceId() = default;
	FInvUI_ContainerInstanceId(const FGameplayTag& InKind, const FGuid& InInstance)
		: KindTag(InKind), Instance(InInstance) {}

	/** True when both the kind tag and the instance guid are set. */
	bool IsValid() const { return KindTag.IsValid() && Instance.IsValid(); }

	/** Mint a fresh instance id of the given kind. */
	static FInvUI_ContainerInstanceId New(const FGameplayTag& InKind)
	{
		return FInvUI_ContainerInstanceId(InKind, FGuid::NewGuid());
	}

	/** The invalid/empty id. */
	static FInvUI_ContainerInstanceId Invalid() { return FInvUI_ContainerInstanceId(); }

	bool operator==(const FInvUI_ContainerInstanceId& Other) const
	{
		return KindTag == Other.KindTag && Instance == Other.Instance;
	}
	bool operator!=(const FInvUI_ContainerInstanceId& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FInvUI_ContainerInstanceId& Id)
	{
		return HashCombine(GetTypeHash(Id.KindTag), GetTypeHash(Id.Instance));
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("%s:%s"),
			*KindTag.ToString(), *Instance.ToString(EGuidFormats::DigitsWithHyphens));
	}
};

/**
 * A single slot's read-only display state, as surfaced by a container to the UI.
 *
 * The slot is addressed by an opaque SlotTag (an identity, NOT an index): a fixed-slot
 * container uses meaningful slot tags (e.g. a "head" / "main-hand" designer tag), a freeform
 * bag mints stable per-slot tags. ItemPayload is an FInstancedStruct carrying whatever extra
 * per-instance data the backend wants the UI to see (durability, sockets, ...); it is a
 * LOCAL/display value and is never itself put on the wire by this module — it arrives already
 * replicated to the client by the backend.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_SlotState
{
	GENERATED_BODY()

	/** Identity of this slot within its container (opaque, never an index). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Slot")
	FGameplayTag SlotTag;

	/** Identity of the item occupying the slot; invalid when the slot is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Slot")
	FGameplayTag ItemTag;

	/** Units in the slot. 0 when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Slot", meta = (ClampMin = "0"))
	int32 Count = 0;

	/** Optional per-instance display payload (durability, sockets, ...). Local/display only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Slot")
	FInstancedStruct ItemPayload;

	FInvUI_SlotState() = default;

	/** True when the slot currently holds an item. */
	bool IsOccupied() const { return ItemTag.IsValid() && Count > 0; }

	/** True when the slot is empty. */
	bool IsEmpty() const { return !IsOccupied(); }

	/**
	 * Cheap field-level comparison used by the GridViewModel to detect which slots actually
	 * changed across a rebuild (so it only re-broadcasts the dirty slot fields). The payload
	 * is compared by serialized identity via FInstancedStruct::Identical semantics.
	 */
	bool EqualsForDisplay(const FInvUI_SlotState& Other) const
	{
		if (SlotTag != Other.SlotTag || ItemTag != Other.ItemTag || Count != Other.Count)
		{
			return false;
		}
		// FInstancedStruct has no operator==; compare struct type + raw memory identity.
		if (ItemPayload.GetScriptStruct() != Other.ItemPayload.GetScriptStruct())
		{
			return false;
		}
		const UScriptStruct* SS = ItemPayload.GetScriptStruct();
		if (SS == nullptr)
		{
			return true; // both empty payloads
		}
		return SS->CompareScriptStruct(ItemPayload.GetMemory(), Other.ItemPayload.GetMemory(), 0);
	}
};

/** Multicast delegate a container fires (server and clients) when its contents change. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInvUI_OnContainerChangedDynamic, const FInvUI_ContainerInstanceId&, ContainerId);

UINTERFACE(MinimalAPI, BlueprintType, meta = (DisplayName = "InvUI Item Container"))
class UInvUI_ItemContainer : public UInterface
{
	GENERATED_BODY()
};

/**
 * The single seam the whole InventoryUI framework binds to. ANY backend (the shipped RPG
 * inventory component, a survival storage, a crafting grid, a vendor stock) implements this
 * interface — usually indirectly via a thin adapter object in its own genre module — so the
 * window UI never hard-depends on a concrete inventory type.
 *
 * IMPORTANT — IDENTITY KEYED: a container is addressed by FInvUI_ContainerInstanceId and its
 * slots by FGameplayTag SlotTag, NEVER by a raw integer index. This keeps bindings stable
 * across replication reorders and lets the server re-derive the authoritative target from the
 * identity instead of trusting a client-named pointer/index.
 *
 * All methods are read-only / query-only. There is no mutator here on purpose: mutation is
 * an *intent* the player-owned intent component routes to the server, which then drives the
 * concrete backend's authority-guarded API. The UI only reads through this seam.
 */
class IInvUI_ItemContainer
{
	GENERATED_BODY()

public:
	/** Stable identity of this container instance (used as the registry key). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Container")
	FInvUI_ContainerInstanceId GetContainerInstanceId() const;

	/**
	 * Fill OutSlots with the current state of every slot (occupied and empty alike for a
	 * fixed-slot container; only present stacks for a freeform bag). Read-only snapshot.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Container")
	void GetSlots(TArray<FInvUI_SlotState>& OutSlots) const;

	/**
	 * Look up a single slot by its identity tag. Returns false (and leaves OutSlot default)
	 * when the container has no such slot.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Container")
	bool GetSlot(FGameplayTag SlotTag, FInvUI_SlotState& OutSlot) const;

	/**
	 * Maximum number of slots the container can present (a fixed equipment set returns its
	 * fixed count; a bag returns its capacity). 0 means "unbounded / driven by contents".
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Container")
	int32 GetCapacity() const;

	/**
	 * Display-side predicate: could the slot identified by SlotTag accept the contents of
	 * Candidate (type/capacity/restriction rules)? This is an ADVISORY check used to grey out
	 * invalid drop targets in the UI; the server re-validates authoritatively before applying
	 * any move. Returns true when the move looks acceptable.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Container")
	bool QueryCanAccept(const FInvUI_SlotState& Candidate, FGameplayTag SlotTag) const;

	/**
	 * Binding hook: return the backend's dynamic change delegate so an adapter / viewmodel can
	 * subscribe to content changes. Backends that surface changes only through an adapter may
	 * return a reference to an empty delegate here and rely on the adapter's native delegate
	 * instead; the GridViewModel prefers the adapter path when one is supplied.
	 *
	 * This is a NATIVE-ONLY virtual (not a UFUNCTION) because UHT cannot generate a reflected
	 * thunk that returns a non-const reference. Implementations return a reference to their
	 * persistent dynamic multicast delegate. The default returns a stable empty delegate so a
	 * backend that never changes (e.g. a read-only vendor preview) need not implement it.
	 */
	virtual FInvUI_OnContainerChangedDynamic& GetOnContainerChangedDelegate()
	{
		static FInvUI_OnContainerChangedDynamic Empty;
		return Empty;
	}
};
