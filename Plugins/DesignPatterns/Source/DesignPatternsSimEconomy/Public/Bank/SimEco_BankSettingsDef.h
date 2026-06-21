// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "SimEco_BankSettingsDef.generated.h"

/**
 * Tag-keyed BANK-RULES data asset: interest rates, loan limits and the reputation gate for credit.
 *
 * Every value is a designer tunable. A bank component references one of these. The bank accrues
 * interest on deposits per economy step and may extend a loan up to a reputation-scaled maximum.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMECONOMY_API USimEco_BankSettingsDef : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimEco_BankSettingsDef();

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

	/** Currency the bank operates in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Bank")
	FGameplayTag CurrencyTag;

	/** Faction whose reputation gates loan size (resolved off the depositor). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Bank")
	FGameplayTag BankFactionTag;

	/**
	 * Interest accrued on the deposit balance PER economy step, as a fraction (0.001 = 0.1% per step).
	 * Compounding is applied each AdvanceEconomyStep. A small per-step rate keeps numbers sane.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Bank",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "0.1"))
	double InterestPerStep = 0.0005;

	/** How many economy steps between interest accruals (0 = every step). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Bank", meta = (ClampMin = "0"))
	int32 InterestEveryNSteps = 1;

	/** Minimum balance below which no interest accrues (avoids paying interest on dust). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Bank", meta = (ClampMin = "0"))
	int64 MinInterestBalance = 1;

	/** Flat base maximum loan a depositor may carry, before the reputation bonus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Bank", meta = (ClampMin = "0"))
	int64 BaseMaxLoan = 0;

	/**
	 * Maps the depositor's reputation with BankFactionTag to a MULTIPLIER on BaseMaxLoan. X = reputation,
	 * Y = multiplier (>=0). Unset => no reputation bonus (multiplier 1.0). High standing unlocks credit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Bank")
	TObjectPtr<UCurveFloat> ReputationToLoanMultiplierCurve = nullptr;

	/** Interest charged on an outstanding loan principal PER step, as a fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Bank",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "0.2"))
	double LoanInterestPerStep = 0.001;

	/** Resolve the reputation-scaled maximum loan for a given reputation value. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Bank")
	int64 ComputeMaxLoan(float Reputation) const;
};
