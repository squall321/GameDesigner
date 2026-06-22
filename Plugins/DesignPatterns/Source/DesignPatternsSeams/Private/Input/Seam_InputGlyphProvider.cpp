// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Input/Seam_InputGlyphProvider.h"

/**
 * Default BlueprintNativeEvent body for ISeam_InputGlyphProvider. The Platform glyph subsystem
 * overrides this; the fail-closed default resolves nothing so an unimplemented seam is safe to call.
 */
bool ISeam_InputGlyphProvider::ResolveActionGlyph_Implementation(
	FGameplayTag /*ActionTag*/, TSoftObjectPtr<UTexture2D>& OutTexture, FText& OutLabel) const
{
	OutTexture = nullptr;
	OutLabel = FText::GetEmpty();
	return false;
}
