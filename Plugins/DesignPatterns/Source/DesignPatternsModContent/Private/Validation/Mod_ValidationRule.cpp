// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Validation/Mod_ValidationRule.h"

#include "Core/DPLog.h"
#include "Data/DPDataAsset.h"

#include "NativeGameplayTags.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#define LOCTEXT_NAMESPACE "ModContentValidation"

namespace ModRuleReasonTags
{
	// Machine-readable reason tags carried on each FMod_ValidationMessage (children of DP.Mod.Reason).
	// Owned by THIS (validation) area so they never collide with the manager area's tag definitions.
	UE_DEFINE_GAMEPLAY_TAG(Reason_MissingTag,       "DP.Mod.Reason.MissingTag");
	UE_DEFINE_GAMEPLAY_TAG(Reason_WrongTagRoot,     "DP.Mod.Reason.WrongTagRoot");
	UE_DEFINE_GAMEPLAY_TAG(Reason_DanglingRef,      "DP.Mod.Reason.DanglingReference");
	UE_DEFINE_GAMEPLAY_TAG(Reason_VersionMismatch,  "DP.Mod.Reason.VersionMismatch");
	UE_DEFINE_GAMEPLAY_TAG(Reason_SandboxEscape,    "DP.Mod.Reason.SandboxEscape");
	UE_DEFINE_GAMEPLAY_TAG(Reason_NotDataAsset,     "DP.Mod.Reason.NotDataAsset");
}

// ---------------------------------------------------------------------------------------------------
// UMod_ValidationRule (base)
// ---------------------------------------------------------------------------------------------------

FMod_ValidationMessage UMod_ValidationRule::MakePass()
{
	FMod_ValidationMessage Msg;
	Msg.Severity = EMod_ValidationResult::Pass;
	return Msg;
}

FMod_ValidationMessage UMod_ValidationRule::MakeMessage(EMod_ValidationResult Severity, const FGameplayTag& Reason, const FText& Detail)
{
	FMod_ValidationMessage Msg;
	Msg.Severity = Severity;
	Msg.Reason = Reason;
	Msg.Detail = Detail;
	return Msg;
}

FMod_ValidationMessage UMod_ValidationRule::Validate_Implementation(const UObject* /*Asset*/) const
{
	// A base/unconfigured rule is inert (passes) so an empty rule slot never spuriously rejects content.
	return MakePass();
}

FName UMod_ValidationRule::GetRuleId_Implementation() const
{
	return GetClass() ? GetClass()->GetFName() : FName(TEXT("UnknownRule"));
}

// ---------------------------------------------------------------------------------------------------
// UMod_ValidationRule_RequiredTagPresent
// ---------------------------------------------------------------------------------------------------

FMod_ValidationMessage UMod_ValidationRule_RequiredTagPresent::Validate_Implementation(const UObject* Asset) const
{
	const UDP_DataAsset* Data = Cast<UDP_DataAsset>(Asset);
	if (Data == nullptr)
	{
		// Not a data asset / failed to load: a tag requirement cannot be satisfied -> fail.
		return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_NotDataAsset,
			LOCTEXT("RequiredTag_NotDataAsset", "Asset is missing or is not a UDP_DataAsset, so its DataTag cannot be validated."));
	}

	if (!Data->DataTag.IsValid())
	{
		return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_MissingTag,
			LOCTEXT("RequiredTag_Empty", "Asset has no DataTag; every pack data asset must carry a stable identity tag."));
	}

	if (RequiredRoot.IsValid() && !Data->DataTag.MatchesTag(RequiredRoot))
	{
		return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_WrongTagRoot,
			FText::Format(
				LOCTEXT("RequiredTag_WrongRoot", "Asset DataTag '{0}' is not under the required content root '{1}'."),
				FText::FromString(Data->DataTag.ToString()),
				FText::FromString(RequiredRoot.ToString())));
	}

	return MakePass();
}

FName UMod_ValidationRule_RequiredTagPresent::GetRuleId_Implementation() const
{
	return FName(TEXT("RequiredTagPresent"));
}

// ---------------------------------------------------------------------------------------------------
// UMod_ValidationRule_NoDanglingSoftRef
// ---------------------------------------------------------------------------------------------------

namespace
{
	/** True if the soft path's referenced asset exists in the AssetRegistry (no load). Null path = ok. */
	bool DoesSoftPathResolve(const IAssetRegistry& AssetRegistry, const FSoftObjectPath& Path)
	{
		if (Path.IsNull())
		{
			return true; // A deliberately-empty optional reference is not dangling.
		}
		const FAssetData Found = AssetRegistry.GetAssetByObjectPath(Path, /*bIncludeOnlyOnDiskAssets=*/false);
		return Found.IsValid();
	}
}

FMod_ValidationMessage UMod_ValidationRule_NoDanglingSoftRef::Validate_Implementation(const UObject* Asset) const
{
	if (Asset == nullptr)
	{
		return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_DanglingRef,
			LOCTEXT("Dangling_NullAsset", "Asset failed to load; soft references cannot be verified."));
	}

	const IAssetRegistry& AssetRegistry =
		FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FString> Dangling;

	// Walk every reflected property collecting soft object/class paths, then confirm each resolves.
	for (TPropertyValueIterator<const FProperty> It(Asset->GetClass(), Asset); It; ++It)
	{
		const FProperty* Property = It->Key;
		const void* ValuePtr = It->Value;

		if (const FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr& SoftPtr = SoftObjProp->GetPropertyValue(ValuePtr);
			if (!DoesSoftPathResolve(AssetRegistry, SoftPtr.ToSoftObjectPath()))
			{
				Dangling.Add(SoftPtr.ToString());
			}
		}
		else if (const FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
		{
			const FSoftObjectPtr& SoftPtr = SoftClassProp->GetPropertyValue(ValuePtr);
			if (!DoesSoftPathResolve(AssetRegistry, SoftPtr.ToSoftObjectPath()))
			{
				Dangling.Add(SoftPtr.ToString());
			}
		}
		else if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			// Raw FSoftObjectPath stored as a struct value (not a typed soft ptr).
			if (StructProp->Struct == TBaseStructure<FSoftObjectPath>::Get())
			{
				const FSoftObjectPath* PathPtr = static_cast<const FSoftObjectPath*>(ValuePtr);
				if (PathPtr && !DoesSoftPathResolve(AssetRegistry, *PathPtr))
				{
					Dangling.Add(PathPtr->ToString());
				}
			}
		}
	}

	if (Dangling.Num() > 0)
	{
		return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_DanglingRef,
			FText::Format(
				LOCTEXT("Dangling_Found", "Asset has {0} dangling soft reference(s): {1}"),
				FText::AsNumber(Dangling.Num()),
				FText::FromString(FString::Join(Dangling, TEXT(", ")))));
	}

	return MakePass();
}

FName UMod_ValidationRule_NoDanglingSoftRef::GetRuleId_Implementation() const
{
	return FName(TEXT("NoDanglingSoftRef"));
}

// ---------------------------------------------------------------------------------------------------
// UMod_ValidationRule_VersionCompatible
// ---------------------------------------------------------------------------------------------------

FMod_ValidationMessage UMod_ValidationRule_VersionCompatible::Validate_Implementation(const UObject* Asset) const
{
	if (Asset == nullptr)
	{
		return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_VersionMismatch,
			LOCTEXT("Version_NullAsset", "Asset failed to load; content version cannot be verified."));
	}

	// Absent version property defaults to 0 (the baseline schema). Read it reflectively so any
	// UDP_DataAsset subclass can opt in simply by declaring an int 'ContentVersion' UPROPERTY.
	int32 Version = 0;
	if (const FIntProperty* VersionProp = FindFProperty<FIntProperty>(Asset->GetClass(), VersionPropertyName))
	{
		Version = VersionProp->GetPropertyValue_InContainer(Asset);
	}

	if (Version < MinSupportedVersion || Version > MaxSupportedVersion)
	{
		return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_VersionMismatch,
			FText::Format(
				LOCTEXT("Version_OutOfRange", "Asset content version {0} is outside the supported range [{1}, {2}]."),
				FText::AsNumber(Version),
				FText::AsNumber(MinSupportedVersion),
				FText::AsNumber(MaxSupportedVersion)));
	}

	return MakePass();
}

FName UMod_ValidationRule_VersionCompatible::GetRuleId_Implementation() const
{
	return FName(TEXT("VersionCompatible"));
}

// ---------------------------------------------------------------------------------------------------
// UMod_ValidationRule_WithinContentRoot
// ---------------------------------------------------------------------------------------------------

FMod_ValidationMessage UMod_ValidationRule_WithinContentRoot::Validate_Implementation(const UObject* Asset) const
{
	if (Asset == nullptr)
	{
		return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_SandboxEscape,
			LOCTEXT("Root_NullAsset", "Asset failed to load; its content root cannot be verified."));
	}

	const UPackage* Package = Asset->GetPackage();
	const FString PackagePath = Package ? Package->GetName() : FString();
	if (PackagePath.IsEmpty())
	{
		return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_SandboxEscape,
			LOCTEXT("Root_NoPackage", "Asset has no package path; cannot confirm it lives in a sandboxed root."));
	}

	// The asset passes if its package begins with any configured or injected (per-pack) allowed root.
	for (const FString& Prefix : AllowedRootPrefixes)
	{
		if (!Prefix.IsEmpty() && PackagePath.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return MakePass();
		}
	}
	for (const FString& Prefix : InjectedPackRoots)
	{
		if (!Prefix.IsEmpty() && PackagePath.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return MakePass();
		}
	}

	return MakeMessage(FailureSeverity, ModRuleReasonTags::Reason_SandboxEscape,
		FText::Format(
			LOCTEXT("Root_Outside", "Asset package '{0}' is outside every sandboxed content root; possible sandbox escape."),
			FText::FromString(PackagePath)));
}

FName UMod_ValidationRule_WithinContentRoot::GetRuleId_Implementation() const
{
	return FName(TEXT("WithinContentRoot"));
}

#undef LOCTEXT_NAMESPACE
