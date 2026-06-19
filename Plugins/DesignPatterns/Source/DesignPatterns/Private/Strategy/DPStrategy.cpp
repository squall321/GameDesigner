// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Strategy/DPStrategy.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

float UDP_Strategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	// Base score is neutral-applicable; subclasses override to weigh by context.
	return 1.f;
}

void UDP_Strategy::Execute_Implementation(const FDP_StrategyContext& Context)
{
	UE_LOG(LogDPFSM, VeryVerbose, TEXT("Strategy '%s' Execute (no-op base)"), *GetDebugName().ToString());
}

FName UDP_Strategy::GetDebugName() const
{
	if (!DebugName.IsNone())
	{
		return DebugName;
	}

	FString Name = GetClass() ? GetClass()->GetName() : GetName();
	Name.RemoveFromStart(TEXT("DP_"));
	Name.RemoveFromEnd(TEXT("_C")); // BP-generated class suffix
	return FName(*Name);
}

UWorld* UDP_Strategy::GetWorldFromContext(const FDP_StrategyContext& Context) const
{
	if (const AActor* OwnerActor = Context.Owner.Get())
	{
		return OwnerActor->GetWorld();
	}
	return nullptr;
}
