// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/Interact_VerbDefinition.h"

UInteract_VerbDefinition::UInteract_VerbDefinition()
{
	// No GetDataAssetType override by design: each verb-definition asset keeps the class-named
	// PrimaryAssetType bucket provided by UDP_DataAsset.
}
