// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Weather/WS_WeatherCarrier.h"

#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

AWS_WeatherCarrier::AWS_WeatherCarrier()
{
	// Replicated carrier. AInfo already disables tick and is hidden; we only ever replicate a tag.
	bReplicates = true;
	bAlwaysRelevant = true;        // one global carrier; always relevant to every client.
	SetReplicatingMovement(false); // no transform — weather is world-global.

	// Sit dormant and only flush when state actually changes, so an unchanging climate costs no
	// per-frame bandwidth.
	NetDormancy = DORM_DormantAll;
}

void AWS_WeatherCarrier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWS_WeatherCarrier, CurrentStateTag);
	DOREPLIFETIME(AWS_WeatherCarrier, StateRevision);
}

bool AWS_WeatherCarrier::AuthSetState(const FGameplayTag& NewState)
{
	// Guard authority at the very top — clients never mutate replicated state.
	if (!HasAuthority())
	{
		return false;
	}

	const bool bTagChanged = (CurrentStateTag != NewState);

	CurrentStateTag = NewState;
	++StateRevision; // force an OnRep even on a same-tag re-apply (forced refresh).

	// Wake from dormancy so the just-changed tag/revision replicates this frame.
	FlushNetDormancy();

	// React on the server through the same delegate path clients will hit via OnRep.
	OnStateChanged.Broadcast(this, CurrentStateTag);

	UE_LOG(LogDP, Verbose, TEXT("WS_WeatherCarrier: auth set weather state '%s' (rev %d, tagChanged=%d)."),
		*CurrentStateTag.ToString(), StateRevision, bTagChanged ? 1 : 0);

	return true;
}

void AWS_WeatherCarrier::OnRep_State()
{
	// Runs on clients after the tag/revision replicate. Fan the change out exactly like the server did.
	OnStateChanged.Broadcast(this, CurrentStateTag);

	UE_LOG(LogDP, Verbose, TEXT("WS_WeatherCarrier: OnRep weather state '%s' (rev %d)."),
		*CurrentStateTag.ToString(), StateRevision);
}
