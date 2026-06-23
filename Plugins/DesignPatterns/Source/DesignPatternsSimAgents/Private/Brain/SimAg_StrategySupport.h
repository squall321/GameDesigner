// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ScriptInterface.h"
#include "GameplayTagContainer.h"
#include "Jobs/SimAg_JobProvider.h"
#include "Jobs/Seam_JobReservation.h"
#include "Threat/Seam_ThreatSense.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "GameFramework/Actor.h"

/**
 * Shared seam-resolution helpers for the deep SimAgents brain strategies (haul / chained-job / interrupt).
 * Header-only inline statics so each strategy translation unit resolves the same way the shipped
 * USimAg_JobStrategy does, without duplicating the logic or exposing it publicly. All resolution is by
 * seam off the owner or via the service locator — never a concrete sibling header.
 */
namespace SimAg_StrategySupport
{
	/** True when Candidate implements UIface; fills a TScriptInterface<IFace> out-param. */
	template <typename UIface, typename IFace>
	bool AdoptInterface(UObject* Candidate, TScriptInterface<IFace>& Out)
	{
		if (Candidate && Candidate->Implements<UIface>())
		{
			Out.SetObject(Candidate);
			Out.SetInterface(Cast<IFace>(Candidate));
			return true;
		}
		return false;
	}

	/**
	 * Resolve the ISimAg_JobProvider seam: off the owning actor first (a per-squad board), then via the
	 * service locator under Service_JobBoard (the world board). Mirrors USimAg_JobStrategy's resolver.
	 */
	inline TScriptInterface<ISimAg_JobProvider> ResolveJobProvider(AActor* Actor)
	{
		TScriptInterface<ISimAg_JobProvider> Result;
		if (!Actor)
		{
			return Result;
		}
		if (AdoptInterface<USimAg_JobProvider, ISimAg_JobProvider>(Actor, Result))
		{
			return Result;
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (AdoptInterface<USimAg_JobProvider, ISimAg_JobProvider>(Comp, Result))
			{
				return Result;
			}
		}
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(Actor))
		{
			if (UObject* Service = Locator->ResolveService(SimAgNativeTags::Service_JobBoard))
			{
				AdoptInterface<USimAg_JobProvider, ISimAg_JobProvider>(Service, Result);
			}
		}
		return Result;
	}

	/** Resolve the ISeam_JobReservation seam via the service locator under Service_JobReservation. */
	inline TScriptInterface<ISeam_JobReservation> ResolveReservation(AActor* Actor)
	{
		TScriptInterface<ISeam_JobReservation> Result;
		if (!Actor)
		{
			return Result;
		}
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(Actor))
		{
			if (UObject* Service = Locator->ResolveService(SimAgNativeTags::Service_JobReservation))
			{
				AdoptInterface<USeam_JobReservation, ISeam_JobReservation>(Service, Result);
			}
		}
		return Result;
	}

	/**
	 * Resolve the ISeam_ThreatSense seam under a project-supplied service key. The key is genre-defined, so
	 * it is passed in (a strategy exposes it as an EditAnywhere FGameplayTag). Invalid if unresolved.
	 */
	inline TScriptInterface<ISeam_ThreatSense> ResolveThreatSense(AActor* Actor, const FGameplayTag& ServiceKey)
	{
		TScriptInterface<ISeam_ThreatSense> Result;
		if (!Actor || !ServiceKey.IsValid())
		{
			return Result;
		}
		// Off the owner first (a per-actor hazard sensor), then the service locator.
		if (AdoptInterface<USeam_ThreatSense, ISeam_ThreatSense>(Actor, Result))
		{
			return Result;
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (AdoptInterface<USeam_ThreatSense, ISeam_ThreatSense>(Comp, Result))
			{
				return Result;
			}
		}
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(Actor))
		{
			if (UObject* Service = Locator->ResolveService(ServiceKey))
			{
				AdoptInterface<USeam_ThreatSense, ISeam_ThreatSense>(Service, Result);
			}
		}
		return Result;
	}
}
