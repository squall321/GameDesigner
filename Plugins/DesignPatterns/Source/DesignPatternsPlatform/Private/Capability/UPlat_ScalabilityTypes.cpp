// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Capability/UPlat_ScalabilityTypes.h"

bool UPlat_ScalabilityProfile::FindBucket(EPlat_PerfTier Tier, FPlat_ScalabilityBucket& Out) const
{
	for (const FPlat_ScalabilityBucket& Bucket : Buckets)
	{
		if (Bucket.Tier == Tier)
		{
			Out = Bucket;
			return true;
		}
	}
	return false;
}

FName UPlat_ScalabilityProfile::GetDataAssetType_Implementation() const
{
	return FName("Plat_ScalabilityProfile");
}
