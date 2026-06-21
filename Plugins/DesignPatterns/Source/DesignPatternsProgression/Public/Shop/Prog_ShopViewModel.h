// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Prog_ShopViewModel.generated.h"

// Used only by pointer below; the full definition is included in the .cpp.
class UProg_ShopComponent;

/**
 * One presentation row for a shop offer: the offer's display data folded together with the viewing
 * player's affordability for it. A pure value type (no gameplay pointers) pushed into the viewmodel so
 * a list/tile widget can bind one widget per row. The display name/icon are resolved by the
 * owner/mediator (e.g. from the data registry) and pushed in, keeping the viewmodel free of registry
 * lookups.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_ShopOfferRow
{
	GENERATED_BODY()

	/** Index of the source entry in the shop definition (the value RequestPurchase takes). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 EntryIndex = INDEX_NONE;

	/** Item identity. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	FGameplayTag ItemTag;

	/** Resolved item display name (pushed by the mediator; empty until resolved). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	FText DisplayName;

	/** Quantity delivered per purchase. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 GrantCount = 1;

	/** Currency the price is in. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	FGameplayTag PriceCurrency;

	/** Unit price. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int64 Price = 0;

	/** Remaining stock; -1 means infinite. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 Remaining = -1;

	/** True if a finite-stock offer has sold out. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	bool bSoldOut = false;

	/** True if the viewing player can currently afford this offer's price. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	bool bAffordable = false;

	/** True if this offer is gated by an unlock condition (UI may grey/lock it). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	bool bLocked = false;

	/** Convenience: the offer is purchasable right now (in stock, affordable, not locked). */
	bool IsPurchasable() const { return !bSoldOut && bAffordable && !bLocked; }
};

/**
 * Observable presentation state for a shop window.
 *
 * Pure pushed display data per the MVVM contract: it holds NO gameplay pointers and never reaches into
 * the world. The window's owner/mediator pushes a refreshed offer list (RefreshOffers) computed from a
 * vendor's UProg_ShopComponent, the viewing player's ISeam_Wallet (for affordability) and the shop's
 * unlock evaluation; the viewmodel stores the resulting rows and broadcasts the observable fields that
 * changed. Widgets bind through the base INotifyFieldValueChanged and re-read the getters.
 *
 * Field notification uses the same hand-rolled IClassDescriptor pattern as the other DP viewmodels:
 * stable EField ids named to match the getter so K2_BroadcastFieldValueChanged resolves by name.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Prog Shop ViewModel"))
class DESIGNPATTERNSPROGRESSION_API UProg_ShopViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	UProg_ShopViewModel();

	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		Title = 0,
		Offers,
		OfferCount,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/**
	 * Push a fully-formed row set (already affordability/lock-resolved by the mediator) into the
	 * viewmodel. Broadcasts Offers/OfferCount when the set differs. Use this when the mediator owns
	 * display-name/icon resolution. Returns true if anything changed.
	 */
	bool SetOffers(const TArray<FProg_ShopOfferRow>& InRows);

	/**
	 * Convenience refresh that builds rows directly from a vendor shop and an optional wallet object:
	 *  - reads offers from Shop->GetOffers,
	 *  - evaluates each entry's unlock gate via the shop for the given buyer (Shop->EvaluateEntryUnlock),
	 *  - reads affordability via the ISeam_Wallet seam on WalletObject (any UObject implementing it).
	 * Display names are NOT resolved here (left empty) so this stays free of registry/world lookups; a
	 * mediator that wants names should use SetOffers with pre-resolved rows. Safe on clients (reads
	 * replicated stock + the read-only wallet seam). Returns true if anything changed.
	 *
	 * @param Shop          The vendor's shop component to read offers/stock/unlock from.
	 * @param Buyer         The viewing buyer actor (for the per-buyer unlock evaluation); may be null.
	 * @param WalletObject  A UObject implementing ISeam_Wallet for affordability; null -> all unaffordable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Shop")
	bool RefreshOffers(const UProg_ShopComponent* Shop, const AActor* Buyer, UObject* WalletObject);

	/** Set the window title text. Broadcasts Title if it changed. */
	UFUNCTION(BlueprintCallable, Category = "Progression|Shop")
	void SetTitle(const FText& InTitle);

	// --- Observable getters (widgets bind by field id / re-read on broadcast) ---

	/** The shop window title. */
	UFUNCTION(BlueprintPure, Category = "Progression|Shop")
	FText GetTitle() const { return Title; }

	/** All current offer rows. */
	UFUNCTION(BlueprintPure, Category = "Progression|Shop")
	const TArray<FProg_ShopOfferRow>& GetOffers() const { return Offers; }

	/** Number of offer rows. */
	UFUNCTION(BlueprintPure, Category = "Progression|Shop")
	int32 GetOfferCount() const { return Offers.Num(); }

	/** Look up a single row by entry index, or an empty row (EntryIndex == INDEX_NONE) if absent. */
	UFUNCTION(BlueprintPure, Category = "Progression|Shop")
	FProg_ShopOfferRow GetOfferByEntryIndex(int32 EntryIndex) const;

	/** Resolve the FFieldId for one of this viewmodel's fields (for mediators / tests). */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** The window title (vendor's effective shop title). */
	UPROPERTY(Transient)
	FText Title;

	/** The current offer rows, in display order. */
	UPROPERTY(Transient)
	TArray<FProg_ShopOfferRow> Offers;
};
