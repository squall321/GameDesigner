// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Input/UPlat_InputGlyphTypes.h"

bool UPlat_GlyphSet::ResolveGlyph(FGameplayTag ActionTag, FPlat_InputGlyph& Out) const
{
	if (!ActionTag.IsValid())
	{
		return false;
	}
	if (const FPlat_InputGlyph* Found = Glyphs.Find(ActionTag))
	{
		Out = *Found;
		return true;
	}
	return false;
}

FName UPlat_GlyphSet::GetDataAssetType_Implementation() const
{
	return FName("Plat_GlyphSet");
}
