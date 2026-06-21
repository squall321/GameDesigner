// Copyright DesignPatterns plugin. All Rights Reserved.

#include "CMC/Move_CharacterMovementComponent.h"
#include "GameFramework/Character.h"

// ============================ UMove_CharacterMovementComponent ============================

UMove_CharacterMovementComponent::UMove_CharacterMovementComponent()
	: bWantsToSprint(0)
	, bWantsToDash(0)
{
}

void UMove_CharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	// Read our two predicted bits from the reserved custom-flag slots. On the server / during a client
	// correction-replay this restores the predicted intent so movement replays identically.
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsToDash   = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

FNetworkPredictionData_Client* UMove_CharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		// The engine owns the lifetime of this object; allocate our subclass once.
		UMove_CharacterMovementComponent* MutableThis = const_cast<UMove_CharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FMove_NetworkPredictionData_Client_Character(*this);
	}
	return ClientPredictionData;
}

// ============================ FMove_SavedMove_Character ============================

void FMove_SavedMove_Character::Clear()
{
	Super::Clear();
	bSavedWantsToSprint = 0;
	bSavedWantsToDash = 0;
}

uint8 FMove_SavedMove_Character::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToSprint)
	{
		Result |= FLAG_Custom_0;
	}
	if (bSavedWantsToDash)
	{
		Result |= FLAG_Custom_1;
	}
	return Result;
}

bool FMove_SavedMove_Character::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	const FMove_SavedMove_Character* New = static_cast<const FMove_SavedMove_Character*>(NewMove.Get());
	if (New)
	{
		// Never combine across a flag edge — that would drop the moment sprint/dash toggled.
		if (bSavedWantsToSprint != New->bSavedWantsToSprint || bSavedWantsToDash != New->bSavedWantsToDash)
		{
			return false;
		}
	}
	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FMove_SavedMove_Character::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
	FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

	if (const UMove_CharacterMovementComponent* CMC =
			Character ? Cast<UMove_CharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr)
	{
		bSavedWantsToSprint = CMC->bWantsToSprint;
		bSavedWantsToDash = CMC->bWantsToDash;
	}
}

// ============================ FMove_NetworkPredictionData_Client_Character ============================

FMove_NetworkPredictionData_Client_Character::FMove_NetworkPredictionData_Client_Character(
	const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FMove_NetworkPredictionData_Client_Character::AllocateNewMove()
{
	return FSavedMovePtr(new FMove_SavedMove_Character());
}
