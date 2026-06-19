// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Replication/UNet_NetUtilsLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

bool UNet_NetUtilsLibrary::HasAuthority(const AActor* Actor)
{
	// Fail-closed: a null actor must never be treated as authoritative.
	return Actor != nullptr && Actor->HasAuthority();
}

bool UNet_NetUtilsLibrary::IsLocallyControlled(const APawn* Pawn)
{
	return Pawn != nullptr && Pawn->IsLocallyControlled();
}

bool UNet_NetUtilsLibrary::IsAutonomousProxy(const AActor* Actor)
{
	return Actor != nullptr && Actor->GetLocalRole() == ROLE_AutonomousProxy;
}

bool UNet_NetUtilsLibrary::IsSimulatedProxy(const AActor* Actor)
{
	return Actor != nullptr && Actor->GetLocalRole() == ROLE_SimulatedProxy;
}

bool UNet_NetUtilsLibrary::IsStandalone(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}
	const UWorld* World = Actor->GetWorld();
	return World != nullptr && World->GetNetMode() == NM_Standalone;
}

bool UNet_NetUtilsLibrary::IsDedicatedServer(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}
	const UWorld* World = Actor->GetWorld();
	return World != nullptr && World->GetNetMode() == NM_DedicatedServer;
}

ENet_NetMode UNet_NetUtilsLibrary::GetNetMode(const AActor* Actor)
{
	if (!Actor)
	{
		return ENet_NetMode::Unknown;
	}
	const UWorld* World = Actor->GetWorld();
	if (!World)
	{
		return ENet_NetMode::Unknown;
	}

	switch (World->GetNetMode())
	{
	case NM_Standalone:       return ENet_NetMode::Standalone;
	case NM_DedicatedServer:  return ENet_NetMode::DedicatedServer;
	case NM_ListenServer:     return ENet_NetMode::ListenServer;
	case NM_Client:           return ENet_NetMode::Client;
	default:                  return ENet_NetMode::Unknown;
	}
}

FString UNet_NetUtilsLibrary::DescribeNetContext(const AActor* Actor)
{
	if (!Actor)
	{
		return TEXT("Net[<null actor>]");
	}

	const TCHAR* ModeStr = TEXT("Unknown");
	switch (GetNetMode(Actor))
	{
	case ENet_NetMode::Standalone:      ModeStr = TEXT("Standalone");      break;
	case ENet_NetMode::DedicatedServer: ModeStr = TEXT("DedicatedServer"); break;
	case ENet_NetMode::ListenServer:    ModeStr = TEXT("ListenServer");    break;
	case ENet_NetMode::Client:          ModeStr = TEXT("Client");          break;
	default:                            ModeStr = TEXT("Unknown");         break;
	}

	const TCHAR* RoleStr = TEXT("None");
	switch (Actor->GetLocalRole())
	{
	case ROLE_Authority:       RoleStr = TEXT("Authority");       break;
	case ROLE_AutonomousProxy: RoleStr = TEXT("AutonomousProxy"); break;
	case ROLE_SimulatedProxy:  RoleStr = TEXT("SimulatedProxy");  break;
	default:                   RoleStr = TEXT("None");            break;
	}

	return FString::Printf(TEXT("Net[%s | Role=%s | Authority=%s] %s"),
		ModeStr, RoleStr,
		Actor->HasAuthority() ? TEXT("true") : TEXT("false"),
		*Actor->GetName());
}

bool UNet_NetUtilsLibrary::EnsureAuthority(const AActor* OwnerActor, const TCHAR* CallSite)
{
	if (OwnerActor && OwnerActor->HasAuthority())
	{
		return true;
	}

	UE_LOG(LogDP, Warning,
		TEXT("EnsureAuthority FAILED at '%s' — replicated-state mutation blocked on a non-authority. "
			 "Route this through a Server RPC."), CallSite);
	return false;
}
