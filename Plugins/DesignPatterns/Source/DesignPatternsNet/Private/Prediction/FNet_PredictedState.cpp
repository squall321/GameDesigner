// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Prediction/FNet_PredictedState.h"

bool FNet_PredictedSnapshot::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// AckedSequence is small and monotonic; serialize the full 32 bits (it is compared for exact
	// equality, so it must be lossless). The state itself rides FSeam_NetValue's compact serializer.
	Ar << AckedSequence;
	Ar << ServerTimeSeconds;

	bool bStateOk = true;
	State.NetSerialize(Ar, Map, bStateOk);

	bOutSuccess = bStateOk;
	return true;
}
