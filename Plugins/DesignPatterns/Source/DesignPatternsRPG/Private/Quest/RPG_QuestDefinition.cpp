// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/RPG_QuestDefinition.h"

FName URPG_QuestDefinition::GetDataAssetType_Implementation() const
{
	return FName(TEXT("RPG_Quest"));
}
