// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Streaming/WorldPartition/Lvl_DataLayerGateComponent.h"
#include "DesignPatternsLevelDirectorNativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Activation/Seam_ActivationGate.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"

// NOTE: this .cpp drives World Partition data layers WITHOUT including any World-Partition header and
// WITHOUT a WorldPartition module dependency — exactly the soft-by-class-name pattern the streaming
// director already uses for UWorldPartitionSubsystem (those typed APIs are unstable across 5.3-5.5).
// We resolve the world's UDataLayerManager and the per-layer instances by reflection, then drive the
// runtime state through the manager's BlueprintCallable SetDataLayerInstanceRuntimeState UFunction.
// When World Partition / data layers are absent the whole path degrades to a documented no-op.

ULvl_DataLayerGateComponent::ULvl_DataLayerGateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Data-layer runtime state replicates through World Partition itself; this component does not.
	SetIsReplicatedByDefault(false);
}

void ULvl_DataLayerGateComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bReevaluateOnBeginPlay)
	{
		Reevaluate();
	}
}

bool ULvl_DataLayerGateComponent::HasWorldAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

UDP_ServiceLocatorSubsystem* ULvl_DataLayerGateComponent::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

bool ULvl_DataLayerGateComponent::IsGateOpen() const
{
	// Invalid key -> ungated (always open).
	if (!GateKey.IsValid())
	{
		return true;
	}
	UDP_ServiceLocatorSubsystem* Locator = GetLocator();
	if (!Locator)
	{
		return true; // documented inert default
	}
	UObject* GateObj = Locator->ResolveService(LvlNativeTags::Service_Lvl_ActivationGate);
	if (!GateObj || !GateObj->GetClass()->ImplementsInterface(USeam_ActivationGate::StaticClass()))
	{
		return true; // gate seam unresolved -> default open
	}
	return ISeam_ActivationGate::Execute_IsGateOpen(GateObj, GateKey);
}

void ULvl_DataLayerGateComponent::Reevaluate()
{
	const bool bOpen = IsGateOpen();

	// Only the authority changes runtime data-layer state; clients receive it via WP replication. We
	// still evaluate the gate everywhere (cheap), but the mutation is authority-gated.
	if (!HasWorldAuthority())
	{
		return;
	}

	ApplyDataLayerState(bOpen);
}

namespace
{
	/**
	 * Resolve the world's UDataLayerManager UObject by reflection (no WP header / module dep). It is an
	 * actor of class /Script/Engine.DataLayerManager owned by the world's WorldPartition; we find it by
	 * iterating the world's objects. Returns null when World Partition / data layers are absent.
	 */
	UObject* ResolveDataLayerManager(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		UClass* ManagerClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.DataLayerManager"));
		if (!ManagerClass)
		{
			return nullptr; // engine band without the DataLayerManager class -> no-op
		}
		// The manager is created per-world; find the instance whose Outer chain leads to this world.
		for (TObjectIterator<UObject> It(/*bOnlyGCedObjects=*/false); It; ++It)
		{
			UObject* Obj = *It;
			if (Obj && Obj->IsA(ManagerClass) && Obj->GetWorld() == World)
			{
				return Obj;
			}
		}
		return nullptr;
	}
}

void ULvl_DataLayerGateComponent::ApplyDataLayerState(bool bGateOpen)
{
	UWorld* World = GetWorld();
	if (!World || DataLayerAssetNames.Num() == 0)
	{
		return;
	}

	UObject* Manager = ResolveDataLayerManager(World);
	if (!Manager)
	{
		// No World Partition / data-layer manager: documented inert no-op.
		UE_LOG(LogDP, Verbose, TEXT("Lvl DataLayerGate (%s): no UDataLayerManager; no-op (gate %s)."),
			*GetNameSafe(GetOwner()), bGateOpen ? TEXT("open") : TEXT("closed"));
		return;
	}

	// Find the manager's BlueprintCallable runtime-state setter by name. Across 5.3-5.5 the manager
	// exposes a "SetDataLayerInstanceRuntimeState"/"SetDataLayerRuntimeStateByName"-family UFunction; we
	// look up whichever is present so we never hard-bind to one minor version's signature.
	static const TCHAR* CandidateFuncs[] = {
		TEXT("SetDataLayerRuntimeStateByName"),
		TEXT("SetDataLayerInstanceRuntimeState")
	};
	UFunction* SetStateFunc = nullptr;
	for (const TCHAR* FuncName : CandidateFuncs)
	{
		if (UFunction* Found = Manager->FindFunction(FName(FuncName)))
		{
			SetStateFunc = Found;
			break;
		}
	}
	if (!SetStateFunc)
	{
		UE_LOG(LogDP, Verbose, TEXT("Lvl DataLayerGate (%s): manager exposes no runtime-state setter; no-op."),
			*GetNameSafe(GetOwner()));
		return;
	}

	// EDataLayerRuntimeState: Unloaded=0, Loaded=1, Activated=2 (stable enum ordering across the band).
	// Closed -> Unloaded; open -> Activated or Loaded per bActivateWhenOpen.
	const uint8 DesiredState = !bGateOpen ? uint8(0) : (bActivateWhenOpen ? uint8(2) : uint8(1));

	int32 Applied = 0;
	for (const FName& AssetName : DataLayerAssetNames)
	{
		if (AssetName.IsNone())
		{
			continue;
		}

		// Build the parameter buffer from the function's own property layout, filling the first FName
		// param with the layer name and the first byte/enum param with the desired state. This is
		// version-robust because we read the property layout rather than assuming an exact signature.
		FStructOnScope ParamScope(SetStateFunc);
		uint8* Params = ParamScope.GetStructMemory();
		bool bSetName = false;
		bool bSetState = false;
		bool bRecursiveAvailable = false;

		for (TFieldIterator<FProperty> ParamIt(SetStateFunc); ParamIt && (ParamIt->PropertyFlags & CPF_Parm); ++ParamIt)
		{
			FProperty* Prop = *ParamIt;
			if (Prop->PropertyFlags & CPF_ReturnParm)
			{
				continue;
			}
			if (!bSetName)
			{
				if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
				{
					NameProp->SetPropertyValue_InContainer(Params, AssetName);
					bSetName = true;
					continue;
				}
			}
			if (!bSetState)
			{
				if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
				{
					ByteProp->SetPropertyValue_InContainer(Params, DesiredState);
					bSetState = true;
					continue;
				}
				if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
				{
					if (FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty())
					{
						Underlying->SetIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(Params),
							static_cast<int64>(DesiredState));
						bSetState = true;
						continue;
					}
				}
			}
			// A trailing bool param ("bIsRecursive") defaults to false, which is fine.
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				BoolProp->SetPropertyValue_InContainer(Params, true);
				bRecursiveAvailable = true;
			}
		}
		(void)bRecursiveAvailable;

		if (bSetName && bSetState)
		{
			Manager->ProcessEvent(SetStateFunc, Params);
			++Applied;
		}
	}

	UE_LOG(LogDP, Log, TEXT("Lvl DataLayerGate (%s): gate %s -> set %d data layer(s) to state %d (%s)."),
		*GetNameSafe(GetOwner()), bGateOpen ? TEXT("open") : TEXT("closed"), Applied, DesiredState,
		*SetStateFunc->GetName());
}
