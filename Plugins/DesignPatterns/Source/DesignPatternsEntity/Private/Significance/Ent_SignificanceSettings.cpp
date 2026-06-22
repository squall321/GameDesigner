// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Significance/Ent_SignificanceSettings.h"

FName UEnt_SignificanceSettings::GetDataAssetType_Implementation() const
{
	static const FName TypeName(TEXT("Ent_SignificanceSettings"));
	return TypeName;
}
