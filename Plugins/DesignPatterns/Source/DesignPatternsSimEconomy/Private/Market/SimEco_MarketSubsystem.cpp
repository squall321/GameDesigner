// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Market/SimEco_MarketSubsystem.h"
#include "Market/SimEco_MarketSettingsDef.h"
#include "Market/SimEco_EconomyReplicationProxy.h"
#include "Settings/SimEco_DeveloperSettings.h"
#include "DesignPatternsSimEconomyModule.h"
#include "Market/SimEco_EconomyTags.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

void USimEco_MarketSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Only the server holds the authoritative book and owns the replicated price proxy.
	if (HasWorldAuthority())
	{
		EnsureReplicationProxy();
	}
}

void USimEco_MarketSubsystem::Deinitialize()
{
	if (HasWorldAuthority() && ReplicationProxy.IsValid())
	{
		ReplicationProxy->Destroy();
	}
	ReplicationProxy.Reset();
	Books.Reset();
	Settings = nullptr;
	bSettingsResolved = false;

	Super::Deinitialize();
}

void USimEco_MarketSubsystem::EnsureReplicationProxy()
{
	if (!HasWorldAuthority() || ReplicationProxy.IsValid())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	ReplicationProxy = World->SpawnActor<ASimEco_EconomyReplicationProxy>(
		ASimEco_EconomyReplicationProxy::StaticClass(), FTransform::Identity, Params);

	if (!ReplicationProxy.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("SimEco market failed to spawn economy replication proxy."));
	}
}

void USimEco_MarketSubsystem::EnsureSettings()
{
	if (bSettingsResolved)
	{
		return;
	}
	bSettingsResolved = true;

	if (Settings)
	{
		return;
	}
	if (const USimEco_DeveloperSettings* Dev = USimEco_DeveloperSettings::Get())
	{
		// Synchronous load is acceptable here: rules are needed before the first clearing and the
		// asset is tiny. Callers that want async control should call SetMarketSettings explicitly.
		Settings = Dev->DefaultMarketSettings.LoadSynchronous();
	}

	if (!Settings)
	{
		UE_LOG(LogDP, Verbose, TEXT("SimEco market has no rules asset; prices stay at base/zero."));
	}
}

void USimEco_MarketSubsystem::SetMarketSettings(USimEco_MarketSettings* InSettings)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	Settings = InSettings;
	bSettingsResolved = true;
}

double USimEco_MarketSubsystem::GetPriceEpsilon() const
{
	if (const USimEco_DeveloperSettings* Dev = USimEco_DeveloperSettings::Get())
	{
		return FMath::Max(0.0, Dev->PriceReplicationEpsilon);
	}
	return 0.0;
}

FSimEco_CommodityBook& USimEco_MarketSubsystem::FindOrAddBook(const FGameplayTag& CommodityTag)
{
	if (FSimEco_CommodityBook* Existing = Books.Find(CommodityTag))
	{
		return *Existing;
	}

	FSimEco_CommodityBook& Book = Books.Add(CommodityTag);
	// Seed the starting price from the rule's base price for the current day, if known.
	EnsureSettings();
	if (Settings)
	{
		const int32 Day = 0; // seed with day 0; clearing later re-anchors using the live clock day.
		const double Base = Settings->GetBasePriceForDay(CommodityTag, Day);
		Book.Price = Base;
	}
	return Book;
}

//~ ISimEco_Market ----------------------------------------------------------------------------

double USimEco_MarketSubsystem::GetPrice_Implementation(FGameplayTag CommodityTag) const
{
	// On a client the authoritative book is empty; read the replicated proxy instead.
	if (!HasWorldAuthority())
	{
		if (ReplicationProxy.IsValid())
		{
			return ReplicationProxy->GetReplicatedPrice(CommodityTag);
		}
		return 0.0;
	}
	if (const FSimEco_CommodityBook* Book = Books.Find(CommodityTag))
	{
		return Book->Price;
	}
	return 0.0;
}

double USimEco_MarketSubsystem::GetSupply_Implementation(FGameplayTag CommodityTag) const
{
	if (const FSimEco_CommodityBook* Book = Books.Find(CommodityTag))
	{
		return Book->Supply;
	}
	return 0.0;
}

double USimEco_MarketSubsystem::GetDemand_Implementation(FGameplayTag CommodityTag) const
{
	if (const FSimEco_CommodityBook* Book = Books.Find(CommodityTag))
	{
		return Book->Demand;
	}
	return 0.0;
}

FSimEco_OrderReceipt USimEco_MarketSubsystem::PlaceOrder_Implementation(const FSimEco_Order& Order)
{
	FSimEco_OrderReceipt Receipt;

	// AUTHORITY GUARD: PlaceOrder is a server-authority API, NOT an RPC. Client calls are rejected.
	if (!HasWorldAuthority())
	{
		UE_LOG(LogDP, Verbose, TEXT("SimEco PlaceOrder rejected on client (route via Server_PlaceOrder)."));
		return Receipt;
	}
	if (!Order.IsValidOrder())
	{
		return Receipt;
	}

	FSimEco_CommodityBook& Book = FindOrAddBook(Order.CommodityTag);

	const double Qty = FMath::Max(0.0, Order.Quantity);
	if (Order.Side == ESimEco_OrderSide::Sell)
	{
		Book.Supply += Qty;
	}
	else
	{
		Book.Demand += Qty;
	}

	Receipt.bAccepted = true;
	Receipt.IndicativePrice = Book.Price;
	Receipt.QueuedQuantity = Qty;
	return Receipt;
}

void USimEco_MarketSubsystem::ClearMarket_Implementation()
{
	// AUTHORITY GUARD: clearing forms authoritative prices; clients never clear.
	if (!HasWorldAuthority())
	{
		return;
	}

	EnsureSettings();

	const double Epsilon = GetPriceEpsilon();

	// Resolve current day for the day-curve base price (clock seam is read-only; ok if absent).
	int32 DayNumber = 0;
	// The market does not own the clock; the economy driver does. We read base-price by day only if
	// settings provide a curve, defaulting to day 0 when no clock context is available here.

	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);

	for (TPair<FGameplayTag, FSimEco_CommodityBook>& Pair : Books)
	{
		const FGameplayTag& CommodityTag = Pair.Key;
		FSimEco_CommodityBook& Book = Pair.Value;

		const double OldPrice = Book.Price;
		double NewPrice = OldPrice;

		const FSimEco_CommodityPriceRule* Rule = Settings ? Settings->FindRule(CommodityTag) : nullptr;
		if (Rule)
		{
			const double Base = Settings->GetBasePriceForDay(CommodityTag, DayNumber);
			const double Norm = FMath::Max(KINDA_SMALL_NUMBER, Settings->ImbalanceNormalization);

			// Normalized excess demand in roughly [-inf,+inf], typically small. Positive => price up.
			const double Imbalance = (Book.Demand - Book.Supply) / Norm;

			// Elastic move plus mean-reversion toward the (possibly seasonal) base price.
			const double Elastic = OldPrice * (1.0 + Rule->Elasticity * Imbalance);
			const double Reverted = FMath::Lerp(Elastic, Base, FMath::Clamp(Rule->MeanReversion, 0.0, 1.0));

			NewPrice = FMath::Clamp(Reverted, Rule->PriceFloor, Rule->PriceCeiling);
		}
		// else: no rule -> price holds steady (no magic default applied).

		Book.Price = NewPrice;

		// Drain the book for the next accumulation window.
		Book.Supply = 0.0;
		Book.Demand = 0.0;

		// Throttled replication + bus broadcast only when the price actually moved.
		if (FMath::Abs(NewPrice - OldPrice) >= Epsilon)
		{
			if (ReplicationProxy.IsValid())
			{
				ReplicationProxy->SyncFromServer(CommodityTag, NewPrice, Epsilon);
			}

			if (Bus)
			{
				FSimEco_PriceChangedMsg Msg;
				Msg.CommodityTag = CommodityTag;
				Msg.OldPrice = OldPrice;
				Msg.NewPrice = NewPrice;

				FInstancedStruct Payload;
				Payload.InitializeAs<FSimEco_PriceChangedMsg>(Msg);
				Bus->BroadcastPayload(SimEcoNativeTags::Bus_PriceChanged, Payload, this);
			}
		}
	}
}

//~ ISeam_Persistable -------------------------------------------------------------------------

void USimEco_MarketSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FSimEco_MarketSaveRecord Record;
	Record.Prices.Reserve(Books.Num());
	for (const TPair<FGameplayTag, FSimEco_CommodityBook>& Pair : Books)
	{
		FSimEco_PersistedPrice& P = Record.Prices.AddDefaulted_GetRef();
		P.CommodityTag = Pair.Key;
		P.Price = Pair.Value.Price;
	}
	Out.InitializeAs<FSimEco_MarketSaveRecord>(Record);
}

void USimEco_MarketSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY GUARD: a client-side load must be a no-op (server replicates prices to it).
	if (!HasWorldAuthority())
	{
		return;
	}
	const FSimEco_MarketSaveRecord* Record = In.GetPtr<FSimEco_MarketSaveRecord>();
	if (!Record)
	{
		return;
	}

	EnsureReplicationProxy();
	const double Epsilon = GetPriceEpsilon();

	for (const FSimEco_PersistedPrice& P : Record->Prices)
	{
		if (!P.CommodityTag.IsValid())
		{
			continue;
		}
		FSimEco_CommodityBook& Book = Books.FindOrAdd(P.CommodityTag);
		Book.Price = P.Price;
		Book.Supply = 0.0;
		Book.Demand = 0.0;

		if (ReplicationProxy.IsValid())
		{
			ReplicationProxy->SyncFromServer(P.CommodityTag, P.Price, Epsilon);
		}
	}
}

FGameplayTag USimEco_MarketSubsystem::GetPersistenceKind_Implementation() const
{
	return SimEcoEconomyTags::Persist_Market;
}

//~ Debug -------------------------------------------------------------------------------------

FString USimEco_MarketSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("SimEco Market: %d commodities, authority=%s, settings=%s"),
		Books.Num(),
		HasWorldAuthority() ? TEXT("yes") : TEXT("no"),
		Settings ? *Settings->GetName() : TEXT("none"));
}
