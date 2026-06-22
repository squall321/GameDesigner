// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Trait/Ent_TraitDefinition.h"

FName UEnt_TraitDefinition::GetDataAssetType_Implementation() const
{
	// One shared asset-manager bucket so the data registry can enumerate all trait definitions together.
	static const FName TypeName(TEXT("Ent_TraitDefinition"));
	return TypeName;
}
