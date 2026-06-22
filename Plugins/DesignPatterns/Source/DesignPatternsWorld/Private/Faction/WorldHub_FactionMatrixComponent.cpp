// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Faction/WorldHub_FactionMatrixComponent.h"
#include "Faction/WorldHub_FactionMatrixDataAsset.h"
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "WorldHub_NativeTags.h"

#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPLog.h"

UWorldHub_FactionMatrixComponent::UWorldHub_FactionMatrixComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWorldHub_FactionMatrixComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveHub();
	RegisterSelfAsService(/*bRegister=*/true);

	if (HasAuthority())
	{
		ApplyInitialMatrix();
	}
}

void UWorldHub_FactionMatrixComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorldHub_StateHubSubsystem* H = Hub.Get())
	{
		H->OnValueChanged.RemoveAll(this);
	}
	RegisterSelfAsService(/*bRegister=*/false);
	StandingIndex.Reset();
	Hub.Reset();

	Super::EndPlay(EndPlayReason);
}

bool UWorldHub_FactionMatrixComponent::HasAuthority() const
{
	const UWorldHub_StateHubSubsystem* H = Hub.Get();
	return H && H->HasWorldAuthority();
}

UWorldHub_StateHubSubsystem* UWorldHub_FactionMatrixComponent::ResolveHub()
{
	if (UWorldHub_StateHubSubsystem* Cached = Hub.Get())
	{
		return Cached;
	}
	UWorldHub_StateHubSubsystem* Resolved =
		FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
	if (Resolved)
	{
		Hub = Resolved;
		Resolved->OnValueChanged.AddUniqueDynamic(this, &UWorldHub_FactionMatrixComponent::OnHubValueChanged);
	}
	return Resolved;
}

//~ Addressing ---------------------------------------------------------------------------------

bool UWorldHub_FactionMatrixComponent::AddressStanding(const FGameplayTag& A, const FGameplayTag& B, FGameplayTag& OutKey, FWorldHub_Scope& OutScope)
{
	if (!A.IsValid() || !B.IsValid())
	{
		return false;
	}
	OutScope = FWorldHub_Scope::Faction(A);
	OutKey = B;
	return true;
}

//~ Apply / project ----------------------------------------------------------------------------

void UWorldHub_FactionMatrixComponent::ApplyInitialMatrix()
{
	if (!InitialMatrix)
	{
		return;
	}
	for (const FWorldHub_FactionStandingSeed& Seed : InitialMatrix->InitialStandings)
	{
		Authority_SetStanding(Seed.FactionA, Seed.FactionB, Seed.Standing);
	}
}

void UWorldHub_FactionMatrixComponent::ProjectStanding(const FGameplayTag& A, const FGameplayTag& B, float Standing)
{
	UWorldHub_StateHubSubsystem* H = ResolveHub();
	FGameplayTag Key;
	FWorldHub_Scope Scope;
	if (!H || !AddressStanding(A, B, Key, Scope))
	{
		return;
	}
	// Standings are stored as floats; the hub projects float net values to the carrier automatically.
	// The replicate/save policy for these slots comes from the data asset's flag definitions (authored
	// in a UWorldHub_FlagSetDataAsset and loaded into the hub) — there are no magic policy bits here.
	H->SetNetValue(Key, FSeam_NetValue::MakeFloat(static_cast<double>(Standing)), Scope);
}

float UWorldHub_FactionMatrixComponent::Authority_SetStanding(FGameplayTag A, FGameplayTag B, float Standing)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasAuthority())
	{
		return 0.0f;
	}
	const float Clamped = InitialMatrix ? InitialMatrix->ClampStanding(Standing) : Standing;

	ProjectStanding(A, B, Clamped);
	if (InitialMatrix && InitialMatrix->Symmetry == EWorldHub_FactionSymmetry::Symmetric)
	{
		ProjectStanding(B, A, Clamped);
	}
	return Clamped;
}

float UWorldHub_FactionMatrixComponent::Authority_AdjustStanding(FGameplayTag A, FGameplayTag B, float Delta)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasAuthority())
	{
		return 0.0f;
	}
	const float Current = GetStanding(A, B);
	return Authority_SetStanding(A, B, Current + Delta);
}

//~ ISeam_FactionStanding ----------------------------------------------------------------------

bool UWorldHub_FactionMatrixComponent::HasFactionMatrix() const
{
	return InitialMatrix != nullptr || StandingIndex.Num() > 0;
}

float UWorldHub_FactionMatrixComponent::GetStanding(FGameplayTag FactionA, FGameplayTag FactionB) const
{
	FGameplayTag Key;
	FWorldHub_Scope Scope;
	if (!AddressStanding(FactionA, FactionB, Key, Scope))
	{
		return 0.0f;
	}
	if (const float* Found = StandingIndex.Find(FWorldHub_SlotAddress(Scope, Key)))
	{
		return *Found;
	}
	// Fall back to the authored default (then 0 when no matrix is configured).
	return InitialMatrix ? InitialMatrix->ClampStanding(InitialMatrix->DefaultStanding) : 0.0f;
}

FGameplayTag UWorldHub_FactionMatrixComponent::GetStandingTier(FGameplayTag FactionA, FGameplayTag FactionB) const
{
	if (!InitialMatrix)
	{
		return FGameplayTag();
	}
	return InitialMatrix->ClassifyTier(GetStanding(FactionA, FactionB));
}

bool UWorldHub_FactionMatrixComponent::AreHostile(FGameplayTag FactionA, FGameplayTag FactionB) const
{
	const float Threshold = InitialMatrix ? InitialMatrix->HostileBelow : 0.0f;
	return GetStanding(FactionA, FactionB) < Threshold;
}

//~ Change tracking ----------------------------------------------------------------------------

void UWorldHub_FactionMatrixComponent::OnHubValueChanged(FWorldHub_Scope Scope, FGameplayTag Key, FSeam_NetValue NewValue)
{
	// Only faction-scoped float slots are ours; keep the read index and the BP delegate in sync on both
	// server and client (clients learn of changes through replicated SyncReplicatedState -> OnValueChanged).
	if (Scope.ScopeType != EWorldHub_ScopeType::Faction || !Key.IsValid())
	{
		return;
	}

	const FWorldHub_SlotAddress Addr(Scope, Key);
	if (NewValue.IsSet() && NewValue.Type == ESeam_NetValueType::Float)
	{
		const float Standing = static_cast<float>(NewValue.FloatValue);
		StandingIndex.Add(Addr, Standing);

		const FGameplayTag A = Scope.FactionTag;
		const FGameplayTag B = Key;
		OnStandingChanged.Broadcast(A, B, Standing);

		if (UDP_MessageBusSubsystem* Bus =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FWorldHub_StandingChangedPayload Payload(A, B, Standing);
			FInstancedStruct Wrapped;
			Wrapped.InitializeAs<FWorldHub_StandingChangedPayload>(Payload);
			Bus->BroadcastPayload(WorldHubNativeTags::Bus_WorldHub_StandingChanged, Wrapped, this);
		}
	}
	else if (!NewValue.IsSet())
	{
		// Cleared slot.
		StandingIndex.Remove(Addr);
	}
}

//~ Service ------------------------------------------------------------------------------------

void UWorldHub_FactionMatrixComponent::RegisterSelfAsService(bool bRegister)
{
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}
	if (bRegister)
	{
		// WeakObserved: the GameInstance-scoped locator must not keep a dead world's component alive.
		Locator->RegisterService(WorldHubNativeTags::Service_WorldHub_FactionMatrix, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
	else if (Locator->ResolveService(WorldHubNativeTags::Service_WorldHub_FactionMatrix) == this)
	{
		Locator->UnregisterService(WorldHubNativeTags::Service_WorldHub_FactionMatrix);
	}
}
