// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Squad/AI_SquadCarrier.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

//~ FAI_SquadMember fast-array callbacks (client side) -------------------------------------------

void FAI_SquadMember::PreReplicatedRemove(const FAI_SquadMemberArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedMemberChange(MemberId);
	}
}

void FAI_SquadMember::PostReplicatedAdd(const FAI_SquadMemberArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedMemberChange(MemberId);
	}
}

void FAI_SquadMember::PostReplicatedChange(const FAI_SquadMemberArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedMemberChange(MemberId);
	}
}

//~ AAI_SquadCarrier ----------------------------------------------------------------------------

AAI_SquadCarrier::AAI_SquadCarrier()
{
	bReplicates = true;
	bAlwaysRelevant = true; // squads are coordination state; keep relevant to every connection
	// A squad changes only on join/leave/role/slot/anchor; start dormant and wake on mutation.
	NetDormancy = DORM_Initial;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	// Wire the fast-array back-pointer so per-item callbacks can notify us (server and client).
	Roster.OwnerCarrier = this;
}

void AAI_SquadCarrier::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Re-assert the back-pointer after any sub-object fixup / net construction.
	Roster.OwnerCarrier = this;
}

void AAI_SquadCarrier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAI_SquadCarrier, SquadId);
	DOREPLIFETIME(AAI_SquadCarrier, SquadTag);
	DOREPLIFETIME(AAI_SquadCarrier, MaxMembers);
	DOREPLIFETIME(AAI_SquadCarrier, AnchorTransform);
	DOREPLIFETIME(AAI_SquadCarrier, Roster);
}

void AAI_SquadCarrier::WakeForChange()
{
	// Move out of dormancy so the just-marked delta is sent; the engine re-sleeps it afterwards.
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
	}
}

//~ Identity ------------------------------------------------------------------------------------

void AAI_SquadCarrier::InitSquad(const FGuid& InSquadId, const FGameplayTag& InSquadTag)
{
	if (!HasAuthority())
	{
		return;
	}
	if (SquadId.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("AI SquadCarrier InitSquad ignored: squad %s already initialised."),
			*SquadId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	}
	SquadId = InSquadId.IsValid() ? InSquadId : FGuid::NewGuid();
	SquadTag = InSquadTag;
	WakeForChange();
}

void AAI_SquadCarrier::SetMaxMembers(int32 InMax)
{
	if (!HasAuthority())
	{
		return;
	}
	MaxMembers = FMath::Max(0, InMax);
	WakeForChange();
}

void AAI_SquadCarrier::SetAnchorTransform(const FTransform& InAnchor)
{
	if (!HasAuthority())
	{
		return;
	}
	AnchorTransform = InAnchor;
	WakeForChange();
	// Authority does not get OnRep; fire the change locally so server-side listeners react too.
	OnSquadChanged.Broadcast(this, FSeam_EntityId::Invalid());
}

//~ Reads ---------------------------------------------------------------------------------------

const FAI_SquadMember* AAI_SquadCarrier::FindMember(const FSeam_EntityId& Member) const
{
	return Roster.Members.FindByPredicate([&Member](const FAI_SquadMember& M) { return M.MemberId == Member; });
}

FAI_SquadMember* AAI_SquadCarrier::FindMemberMutable(const FSeam_EntityId& Member)
{
	return Roster.Members.FindByPredicate([&Member](const FAI_SquadMember& M) { return M.MemberId == Member; });
}

FGameplayTag AAI_SquadCarrier::GetRole(const FSeam_EntityId& Member) const
{
	const FAI_SquadMember* Found = FindMember(Member);
	return Found ? Found->Role : FGameplayTag();
}

FTransform AAI_SquadCarrier::GetAbsoluteSlot(const FSeam_EntityId& Member) const
{
	const FAI_SquadMember* Found = FindMember(Member);
	if (!Found)
	{
		return AnchorTransform;
	}
	// Absolute slot = anchor composed with the member's relative slot.
	return Found->RelativeSlot * AnchorTransform;
}

//~ Authority mutators --------------------------------------------------------------------------

bool AAI_SquadCarrier::AddMember(const FSeam_EntityId& Member)
{
	if (!HasAuthority())
	{
		return false;
	}
	if (!Member.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("AI SquadCarrier AddMember rejected: invalid member id."));
		return false;
	}
	if (FindMember(Member))
	{
		return false; // already present
	}
	if (MaxMembers > 0 && Roster.Members.Num() >= MaxMembers)
	{
		UE_LOG(LogDP, Verbose, TEXT("AI SquadCarrier AddMember rejected: squad %s at cap %d."),
			*SquadId.ToString(EGuidFormats::DigitsWithHyphens), MaxMembers);
		return false;
	}

	FAI_SquadMember& Added = Roster.Members.Emplace_GetRef(Member);
	Roster.MarkItemDirty(Added);
	WakeForChange();
	OnSquadChanged.Broadcast(this, Member);
	return true;
}

bool AAI_SquadCarrier::RemoveMember(const FSeam_EntityId& Member)
{
	if (!HasAuthority())
	{
		return false;
	}
	const int32 Index = Roster.Members.IndexOfByPredicate(
		[&Member](const FAI_SquadMember& M) { return M.MemberId == Member; });
	if (Index == INDEX_NONE)
	{
		return false;
	}

	Roster.Members.RemoveAt(Index);
	Roster.MarkArrayDirty();
	WakeForChange();
	OnSquadChanged.Broadcast(this, Member);
	return true;
}

bool AAI_SquadCarrier::ClaimRole(const FSeam_EntityId& Member, const FGameplayTag& Role, bool bExclusive)
{
	if (!HasAuthority())
	{
		return false;
	}
	FAI_SquadMember* Target = FindMemberMutable(Member);
	if (!Target)
	{
		UE_LOG(LogDP, Verbose, TEXT("AI SquadCarrier ClaimRole rejected: %s not in squad."), *Member.ToString());
		return false;
	}

	if (bExclusive && Role.IsValid())
	{
		// Clear the role from any other member that currently holds it so it stays unique.
		for (FAI_SquadMember& Other : Roster.Members)
		{
			if (Other.MemberId != Member && Other.Role == Role)
			{
				Other.Role = FGameplayTag();
				Roster.MarkItemDirty(Other);
				OnSquadChanged.Broadcast(this, Other.MemberId);
			}
		}
	}

	Target->Role = Role;
	Roster.MarkItemDirty(*Target);
	WakeForChange();
	OnSquadChanged.Broadcast(this, Member);
	return true;
}

bool AAI_SquadCarrier::AssignSlot(const FSeam_EntityId& Member, const FTransform& RelativeSlot)
{
	if (!HasAuthority())
	{
		return false;
	}
	FAI_SquadMember* Target = FindMemberMutable(Member);
	if (!Target)
	{
		return false;
	}

	Target->RelativeSlot = RelativeSlot;
	Roster.MarkItemDirty(*Target);
	WakeForChange();
	OnSquadChanged.Broadcast(this, Member);
	return true;
}

//~ Replication callbacks -----------------------------------------------------------------------

void AAI_SquadCarrier::OnRep_AnchorTransform()
{
	// A whole-squad change: movers re-read their absolute slots against the new anchor.
	OnSquadChanged.Broadcast(this, FSeam_EntityId::Invalid());
}

void AAI_SquadCarrier::HandleReplicatedMemberChange(const FSeam_EntityId& Member)
{
	OnSquadChanged.Broadcast(this, Member);
}
