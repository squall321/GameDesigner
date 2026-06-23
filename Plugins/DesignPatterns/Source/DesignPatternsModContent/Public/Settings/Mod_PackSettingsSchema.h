// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Mod_PackSettingsSchema.generated.h"

/**
 * The value kind of one declared pack setting. Constrains the editor UI and the (optional) clamp meta a
 * field may carry. The actual default/stored value is an FInstancedStruct so a project can use whatever
 * concrete value struct it likes; this enum is the coarse classification for tooling and validation.
 */
UENUM(BlueprintType)
enum class EMod_SettingKind : uint8
{
	/** A boolean toggle. */
	Bool,

	/** An integer value (clamp meta interpreted as integer bounds). */
	Int,

	/** A floating-point value (clamp meta interpreted as float bounds). */
	Float,

	/** A gameplay tag (e.g. a difficulty / variant selector). */
	Tag,

	/** A short free-form string (e.g. a name). Never used for identity or security. */
	String
};

/**
 * One DECLARED setting in a pack's schema: its stable field id, its kind, an optional default value
 * (carried as an FInstancedStruct so the schema is type-agnostic), human-facing label/tooltip, and
 * optional numeric clamp bounds. Pure metadata that travels with the pack.
 *
 * FInstancedStruct is only ever used here (a data asset) — never plain-Replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_PackSettingField
{
	GENERATED_BODY()

	/** Stable identity of this setting within the pack (child of a project DP.Mod.Setting.* namespace). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Settings")
	FGameplayTag FieldId;

	/** The value kind (drives editor UI + clamp interpretation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Settings")
	EMod_SettingKind Kind = EMod_SettingKind::Bool;

	/**
	 * The default value, as an opaque value struct. When empty the config store synthesises a zero/empty
	 * default for the kind. The concrete struct type is the project's choice (e.g. a simple bool/int/float
	 * wrapper) — the schema never inspects it beyond Kind.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Settings")
	FInstancedStruct DefaultValue;

	/** Human-facing label for the settings UI. Localizable; cosmetic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Settings")
	FText Label;

	/** Optional human-facing tooltip. Localizable; cosmetic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Settings", meta = (MultiLine = true))
	FText Tooltip;

	/** Lower numeric clamp (Int/Float kinds). Ignored for non-numeric kinds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Settings")
	float ClampMin = 0.f;

	/** Upper numeric clamp (Int/Float kinds). Must be >= ClampMin to be honoured; ignored otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ModContent|Settings")
	float ClampMax = 0.f;

	/** True when ClampMin/ClampMax form a usable range (Max strictly greater than Min). */
	bool HasClampRange() const { return ClampMax > ClampMin; }
};

/**
 * One STORED setting value: the field it answers plus its current value as an FInstancedStruct. Used by
 * the player config store and carried in the settings-changed bus payload / save record. Never plain-
 * Replicated (FInstancedStruct rides only data assets / save records / bus payloads).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_PackSettingValue
{
	GENERATED_BODY()

	/** The field this value answers (matches a FMod_PackSettingField::FieldId in the pack's schema). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ModContent|Settings")
	FGameplayTag FieldId;

	/** The stored value, as an opaque value struct (same shape as the schema field's default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ModContent|Settings")
	FInstancedStruct Value;

	FMod_PackSettingValue() = default;
	FMod_PackSettingValue(FGameplayTag InFieldId, const FInstancedStruct& InValue)
		: FieldId(InFieldId), Value(InValue) {}
};

/**
 * Per-pack settings SCHEMA — pure metadata that travels INSIDE a pack (like its descriptor) describing
 * the player-facing options the pack exposes. A UDP_DataAsset so it is tag-addressable and validated
 * exactly like the rest of the content; its DataTag identifies the owning pack (a child of DP.Mod.Pack).
 *
 * It declares fields only; it stores no player values. The player's chosen values live in the
 * GameInstance-scoped UMod_PackConfigStoreSubsystem, validated against this schema.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Mod Pack Settings Schema"))
class DESIGNPATTERNSMODCONTENT_API UMod_PackSettingsSchema : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The settings this pack exposes to the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schema")
	TArray<FMod_PackSettingField> Fields;

	/** The owning pack id (always equals the inherited DataTag). */
	FGameplayTag GetOwningPackId() const { return DataTag; }

	/** Find a declared field by id. Returns nullptr when absent. */
	const FMod_PackSettingField* FindField(FGameplayTag FieldId) const
	{
		return Fields.FindByPredicate([FieldId](const FMod_PackSettingField& F) { return F.FieldId == FieldId; });
	}

	//~ Begin UDP_DataAsset
	/** Group all pack settings schemas under a single asset-manager bucket so they enumerate together. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags an owning DataTag not under DP.Mod.Pack, duplicate field ids, and inverted clamp ranges. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
