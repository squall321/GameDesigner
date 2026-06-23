// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Prompt/DPInputPromptWidget.h"
#include "DPUINativeTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Input/Seam_InputGlyphProvider.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

void UDP_InputPromptWidget::SetActionTag(FGameplayTag InActionTag)
{
	ActionTag = InActionTag;
	if (IsConstructed())
	{
		RefreshGlyph();
	}
}

void UDP_InputPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SubscribeDeviceChanged();
	RefreshGlyph();
}

void UDP_InputPromptWidget::NativeDestruct()
{
	// Remove our bus listener so it does not fire on a destroyed widget.
	if (DeviceChangedHandle.IsValid())
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			Bus->StopListening(DeviceChangedHandle);
		}
		DeviceChangedHandle = FDP_ListenerHandle();
	}
	Super::NativeDestruct();
}

UDP_ServiceLocatorSubsystem* UDP_InputPromptWidget::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

void UDP_InputPromptWidget::SubscribeDeviceChanged()
{
	if (DeviceChangedHandle.IsValid())
	{
		return;
	}

	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	TWeakObjectPtr<UDP_InputPromptWidget> WeakThis(this);
	DeviceChangedHandle = Bus->ListenNative(
		DPUITags::Bus_InputDeviceChanged,
		[WeakThis](const FDP_Message& /*Message*/)
		{
			if (UDP_InputPromptWidget* Strong = WeakThis.Get())
			{
				Strong->RefreshGlyph();
			}
		},
		this);
}

void UDP_InputPromptWidget::RefreshGlyph()
{
	if (!ActionTag.IsValid())
	{
		ApplyResolved(/*bHasGlyph*/ false, FText::GetEmpty());
		return;
	}

	UDP_ServiceLocatorSubsystem* Locator = GetLocator();
	UObject* ProviderObj = Locator ? Locator->ResolveService(DPUITags::Service_Glyphs) : nullptr;
	if (!ProviderObj || !ProviderObj->Implements<USeam_InputGlyphProvider>())
	{
		// No provider — show nothing (or the empty label). Inert, not an error.
		ApplyResolved(/*bHasGlyph*/ false, FText::GetEmpty());
		return;
	}

	TSoftObjectPtr<UTexture2D> GlyphSoft;
	FText Label;
	const bool bResolved = ISeam_InputGlyphProvider::Execute_ResolveActionGlyph(ProviderObj, ActionTag, GlyphSoft, Label);

	PendingLabel = Label;

	if (!bResolved || GlyphSoft.IsNull())
	{
		// Label-only (no glyph mapped on this device).
		PendingGlyph.Reset();
		ApplyResolved(/*bHasGlyph*/ false, Label);
		return;
	}

	PendingGlyph = GlyphSoft;

	if (UTexture2D* Already = GlyphSoft.Get())
	{
		// Already loaded — apply synchronously.
		if (GlyphImage)
		{
			GlyphImage->SetBrushFromTexture(Already);
		}
		ApplyResolved(/*bHasGlyph*/ true, Label);
		return;
	}

	// Async-load the glyph texture so we never force-load the atlas on the game thread.
	if (UAssetManager* AssetManager = UAssetManager::GetIfInitialized())
	{
		TWeakObjectPtr<UDP_InputPromptWidget> WeakThis(this);
		const FSoftObjectPath Path = GlyphSoft.ToSoftObjectPath();
		AssetManager->GetStreamableManager().RequestAsyncLoad(
			Path,
			FStreamableDelegate::CreateLambda([WeakThis, Path]()
			{
				if (UDP_InputPromptWidget* Strong = WeakThis.Get())
				{
					// Only apply if the soft target still matches (the device may have changed mid-load).
					if (Strong->PendingGlyph.ToSoftObjectPath() == Path)
					{
						Strong->OnGlyphTextureLoaded();
					}
				}
			}));
	}
	else
	{
		// No asset manager (early/editor) — best-effort synchronous load.
		if (UTexture2D* Loaded = GlyphSoft.LoadSynchronous())
		{
			if (GlyphImage)
			{
				GlyphImage->SetBrushFromTexture(Loaded);
			}
			ApplyResolved(/*bHasGlyph*/ true, Label);
		}
		else
		{
			ApplyResolved(/*bHasGlyph*/ false, Label);
		}
	}
}

void UDP_InputPromptWidget::OnGlyphTextureLoaded()
{
	UTexture2D* Texture = PendingGlyph.Get();
	if (Texture && GlyphImage)
	{
		GlyphImage->SetBrushFromTexture(Texture);
	}
	ApplyResolved(/*bHasGlyph*/ Texture != nullptr, PendingLabel);
}

void UDP_InputPromptWidget::ApplyResolved(bool bHasGlyph, const FText& Label)
{
	if (LabelText)
	{
		LabelText->SetText(Label);
	}
	OnGlyphResolved(bHasGlyph, Label);
}
