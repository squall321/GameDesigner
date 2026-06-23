// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Jobs/SimAg_SkillComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

USimAg_SkillComponent::USimAg_SkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USimAg_SkillComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Skills = FGameplayTagContainer();
		SkillLevels.Reset();
		for (const FSimAg_SkillLevel& Default : DefaultSkills)
		{
			if (!Default.Skill.IsValid())
			{
				continue;
			}
			Skills.AddTag(Default.Skill);
			FSimAg_SkillLevel Level = Default;
			Level.Level = FMath::Max(0.f, Level.Level);
			SkillLevels.Add(Level);
		}
		OnSkillsChanged.Broadcast(this);
	}
}

void USimAg_SkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimAg_SkillComponent, Skills);
	DOREPLIFETIME(USimAg_SkillComponent, SkillLevels);
}

bool USimAg_SkillComponent::HasSkill(FGameplayTag RequiredSkill) const
{
	if (!RequiredSkill.IsValid())
	{
		return true; // no requirement
	}
	// Hierarchy match: an agent with "Job.Skill.Crafting.Smithing" satisfies a requirement of
	// "Job.Skill.Crafting".
	return Skills.HasTag(RequiredSkill);
}

float USimAg_SkillComponent::GetSkillLevel(FGameplayTag Skill) const
{
	for (const FSimAg_SkillLevel& Level : SkillLevels)
	{
		if (Level.Skill == Skill)
		{
			return Level.Level;
		}
	}
	return 0.f;
}

void USimAg_SkillComponent::GrantSkill(FGameplayTag Skill, float Level)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!Skill.IsValid())
	{
		return;
	}
	const float Clamped = FMath::Max(0.f, Level);
	if (FSimAg_SkillLevel* Existing = FindLevel(Skill))
	{
		Existing->Level = Clamped;
	}
	else
	{
		SkillLevels.Add(FSimAg_SkillLevel(Skill, Clamped));
	}
	Skills.AddTag(Skill);
	OnSkillsChanged.Broadcast(this);
}

bool USimAg_SkillComponent::RevokeSkill(FGameplayTag Skill)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const int32 Removed = SkillLevels.RemoveAll([&Skill](const FSimAg_SkillLevel& L) { return L.Skill == Skill; });
	const bool bHad = Skills.HasTagExact(Skill);
	Skills.RemoveTag(Skill);
	if (Removed > 0 || bHad)
	{
		OnSkillsChanged.Broadcast(this);
		return true;
	}
	return false;
}

void USimAg_SkillComponent::OnRep_Skills()
{
	OnSkillsChanged.Broadcast(this);
}

FSimAg_SkillLevel* USimAg_SkillComponent::FindLevel(const FGameplayTag& Skill)
{
	return SkillLevels.FindByPredicate([&Skill](const FSimAg_SkillLevel& L) { return L.Skill == Skill; });
}
