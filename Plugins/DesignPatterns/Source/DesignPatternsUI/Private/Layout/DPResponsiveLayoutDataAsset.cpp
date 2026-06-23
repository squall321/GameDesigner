// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Layout/DPResponsiveLayoutDataAsset.h"

EDP_UIBreakpoint UDP_ResponsiveLayoutDataAsset::ClassifyWidth(int32 Width) const
{
	if (Width <= CompactMaxWidth)
	{
		return EDP_UIBreakpoint::Compact;
	}
	if (Width <= MediumMaxWidth)
	{
		return EDP_UIBreakpoint::Medium;
	}
	if (Width <= ExpandedMaxWidth)
	{
		return EDP_UIBreakpoint::Expanded;
	}
	return EDP_UIBreakpoint::Wide;
}

FName UDP_ResponsiveLayoutDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("DP_ResponsiveLayout"));
}
