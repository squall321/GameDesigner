// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Shop/Prog_ShopViewModel.h"
#include "Shop/Prog_ShopComponent.h"
#include "Economy/Seam_Wallet.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/**
	 * Hand-rolled descriptor enumerating UProg_ShopViewModel's observable fields. One FFieldId per
	 * EField value, named to match the getter so K2_BroadcastFieldValueChanged can resolve them by name.
	 */
	struct FProg_ShopViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UProg_ShopViewModel::EField::Num];

		static FFieldId MakeId(UProg_ShopViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UProg_ShopViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FProg_ShopViewModelDescriptor::FieldNames[(int32)UProg_ShopViewModel::EField::Num] =
	{
		FName(TEXT("Title")),
		FName(TEXT("Offers")),
		FName(TEXT("OfferCount")),
	};

	static const FProg_ShopViewModelDescriptor GProg_ShopViewModelDescriptor;
}

namespace
{
	/** Field-level equality for an offer row (drives the change-detection in SetOffers). */
	bool RowsEqual(const FProg_ShopOfferRow& A, const FProg_ShopOfferRow& B)
	{
		return A.EntryIndex == B.EntryIndex
			&& A.ItemTag == B.ItemTag
			&& A.DisplayName.IdenticalTo(B.DisplayName)
			&& A.GrantCount == B.GrantCount
			&& A.PriceCurrency == B.PriceCurrency
			&& A.Price == B.Price
			&& A.Remaining == B.Remaining
			&& A.bSoldOut == B.bSoldOut
			&& A.bAffordable == B.bAffordable
			&& A.bLocked == B.bLocked;
	}

	/** True if two row arrays are element-wise equal. */
	bool RowArraysEqual(const TArray<FProg_ShopOfferRow>& A, const TArray<FProg_ShopOfferRow>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!RowsEqual(A[Index], B[Index]))
			{
				return false;
			}
		}
		return true;
	}
}

UProg_ShopViewModel::UProg_ShopViewModel()
{
}

const UE::FieldNotification::IClassDescriptor& UProg_ShopViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GProg_ShopViewModelDescriptor;
}

UE::FieldNotification::FFieldId UProg_ShopViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FProg_ShopViewModelDescriptor::MakeId(Field);
}

void UProg_ShopViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UProg_ShopViewModel::SetTitle(const FText& InTitle)
{
	if (!Title.IdenticalTo(InTitle))
	{
		Title = InTitle;
		BroadcastField(EField::Title);
	}
}

bool UProg_ShopViewModel::SetOffers(const TArray<FProg_ShopOfferRow>& InRows)
{
	if (RowArraysEqual(Offers, InRows))
	{
		return false;
	}

	const int32 OldCount = Offers.Num();
	Offers = InRows;

	BroadcastField(EField::Offers);
	if (Offers.Num() != OldCount)
	{
		BroadcastField(EField::OfferCount);
	}
	return true;
}

bool UProg_ShopViewModel::RefreshOffers(const UProg_ShopComponent* Shop, const AActor* Buyer, UObject* WalletObject)
{
	TArray<FProg_ShopOfferRow> NewRows;

	if (Shop)
	{
		// Pull the projected offers (display data + live remaining stock) from the vendor.
		TArray<FProg_ShopOffer> Offers0;
		Shop->GetOffers(Offers0);

		// Resolve the wallet seam once (read-only, client-safe). Null -> nothing is affordable.
		const bool bWalletValid = WalletObject && WalletObject->GetClass()->ImplementsInterface(USeam_Wallet::StaticClass());

		NewRows.Reserve(Offers0.Num());
		for (const FProg_ShopOffer& Offer : Offers0)
		{
			FProg_ShopOfferRow Row;
			Row.EntryIndex = Offer.EntryIndex;
			Row.ItemTag = Offer.ItemTag;
			Row.GrantCount = Offer.GrantCount;
			Row.PriceCurrency = Offer.PriceCurrency;
			Row.Price = Offer.Price;
			Row.Remaining = Offer.Remaining;
			Row.bSoldOut = Offer.bSoldOut;

			// Affordability: free items (price 0) are always affordable; otherwise ask the wallet seam.
			const bool bIsPaid = Offer.Price > 0 && Offer.PriceCurrency.IsValid();
			Row.bAffordable = !bIsPaid
				|| (bWalletValid && ISeam_Wallet::Execute_CanAfford(WalletObject, Offer.PriceCurrency, Offer.Price));

			// Lock state: only meaningful if the offer is gated; ask the shop to evaluate it for the buyer.
			Row.bLocked = Offer.bHasUnlockGate && !Shop->EvaluateEntryUnlock(Buyer, Offer.EntryIndex);

			NewRows.Add(MoveTemp(Row));
		}
	}
	else
	{
		UE_LOG(LogDP, Verbose, TEXT("[Prog_ShopVM] RefreshOffers with null shop; clearing offers."));
	}

	return SetOffers(NewRows);
}

FProg_ShopOfferRow UProg_ShopViewModel::GetOfferByEntryIndex(int32 EntryIndex) const
{
	if (const FProg_ShopOfferRow* Found =
		Offers.FindByPredicate([EntryIndex](const FProg_ShopOfferRow& R) { return R.EntryIndex == EntryIndex; }))
	{
		return *Found;
	}
	return FProg_ShopOfferRow();
}
