// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Core/DPSubsystemBases.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------------------
// UDP_GameInstanceSubsystem
// ---------------------------------------------------------------------------------------

void UDP_GameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Verbose, TEXT("[%s] Initialized (GameInstance subsystem)."), *GetClass()->GetName());
}

void UDP_GameInstanceSubsystem::Deinitialize()
{
	UE_LOG(LogDP, Verbose, TEXT("[%s] Deinitialized (GameInstance subsystem)."), *GetClass()->GetName());
	Super::Deinitialize();
}

FString UDP_GameInstanceSubsystem::GetDPDebugString_Implementation() const
{
	return GetClass()->GetName();
}

// ---------------------------------------------------------------------------------------
// UDP_WorldSubsystem
// ---------------------------------------------------------------------------------------

void UDP_WorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Verbose, TEXT("[%s] Initialized (World subsystem)."), *GetClass()->GetName());
}

void UDP_WorldSubsystem::Deinitialize()
{
	UE_LOG(LogDP, Verbose, TEXT("[%s] Deinitialized (World subsystem)."), *GetClass()->GetName());
	Super::Deinitialize();
}

bool UDP_WorldSubsystem::IsSupportedWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UDP_WorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// Outer is the UWorld for world subsystems.
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return IsSupportedWorldType(World->WorldType);
	}
	return true;
}

FString UDP_WorldSubsystem::GetDPDebugString_Implementation() const
{
	return GetClass()->GetName();
}
