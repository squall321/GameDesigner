// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "DPBlackboard.generated.h"

/**
 * Blackboard value type discriminator. Selects which storage map a key reads/writes.
 */
UENUM(BlueprintType)
enum class EDP_BlackboardValueType : uint8
{
	None,
	Bool,
	Int,
	Float,
	Vector,
	Object
};

/**
 * Read/write seam onto a typed key/value store used by states, guards and strategies.
 *
 * This interface exists so the FSM/Strategy code never has to know whether the backing
 * store is the plugin's own UDP_Blackboard, an AIModule UBlackboardComponent adapter, or
 * any game-specific data bag. Crucially it keeps AIModule out of this module's link line.
 */
UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UDP_BlackboardProvider : public UInterface
{
	GENERATED_BODY()
};

/** @see UDP_BlackboardProvider */
class DESIGNPATTERNS_API IDP_BlackboardProvider
{
	GENERATED_BODY()

public:
	/** @return true if a value (of any type) is currently stored under Key. */
	virtual bool HasKey(FName Key) const = 0;

	/** @return the value type stored under Key, or None when absent. */
	virtual EDP_BlackboardValueType GetKeyType(FName Key) const = 0;

	virtual bool GetBool(FName Key, bool bDefault = false) const = 0;
	virtual int32 GetInt(FName Key, int32 Default = 0) const = 0;
	virtual float GetFloat(FName Key, float Default = 0.f) const = 0;
	virtual FVector GetVector(FName Key, const FVector& Default = FVector::ZeroVector) const = 0;
	virtual UObject* GetObject(FName Key) const = 0;

	virtual void SetBool(FName Key, bool bValue) = 0;
	virtual void SetInt(FName Key, int32 Value) = 0;
	virtual void SetFloat(FName Key, float Value) = 0;
	virtual void SetVector(FName Key, const FVector& Value) = 0;
	virtual void SetObject(FName Key, UObject* Value) = 0;

	/** Remove any value stored under Key. @return true if a value was removed. */
	virtual bool ClearKey(FName Key) = 0;
};

/**
 * Concrete per-instance typed key/value store implementing IDP_BlackboardProvider.
 *
 * Owned by a UDP_StateMachineComponent (one per FSM instance). Values are split into a
 * small set of typed maps keyed by FName so reads are allocation-free and BP-exposable.
 * Object references are held as UPROPERTY TObjectPtr so they keep their referents alive and
 * are GC-visible while stored on the blackboard.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNS_API UDP_Blackboard : public UObject, public IDP_BlackboardProvider
{
	GENERATED_BODY()

public:
	//~ Begin IDP_BlackboardProvider
	virtual bool HasKey(FName Key) const override;
	virtual EDP_BlackboardValueType GetKeyType(FName Key) const override;
	virtual bool GetBool(FName Key, bool bDefault = false) const override;
	virtual int32 GetInt(FName Key, int32 Default = 0) const override;
	virtual float GetFloat(FName Key, float Default = 0.f) const override;
	virtual FVector GetVector(FName Key, const FVector& Default = FVector::ZeroVector) const override;
	virtual UObject* GetObject(FName Key) const override;
	virtual void SetBool(FName Key, bool bValue) override;
	virtual void SetInt(FName Key, int32 Value) override;
	virtual void SetFloat(FName Key, float Value) override;
	virtual void SetVector(FName Key, const FVector& Value) override;
	virtual void SetObject(FName Key, UObject* Value) override;
	virtual bool ClearKey(FName Key) override;
	//~ End IDP_BlackboardProvider

	/** @return true if any value (of any type) is stored under Key. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM|Blackboard")
	bool BP_HasKey(FName Key) const { return HasKey(Key); }

	/** @return the value type stored under Key, or None when absent. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM|Blackboard")
	EDP_BlackboardValueType BP_GetKeyType(FName Key) const { return GetKeyType(Key); }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM|Blackboard")
	bool BP_GetBool(FName Key, bool bDefault = false) const { return GetBool(Key, bDefault); }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM|Blackboard")
	int32 BP_GetInt(FName Key, int32 Default = 0) const { return GetInt(Key, Default); }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM|Blackboard")
	float BP_GetFloat(FName Key, float Default = 0.f) const { return GetFloat(Key, Default); }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM|Blackboard")
	FVector BP_GetVector(FName Key, FVector Default = FVector::ZeroVector) const { return GetVector(Key, Default); }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM|Blackboard")
	UObject* BP_GetObject(FName Key) const { return GetObject(Key); }

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM|Blackboard")
	void BP_SetBool(FName Key, bool bValue) { SetBool(Key, bValue); }

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM|Blackboard")
	void BP_SetInt(FName Key, int32 Value) { SetInt(Key, Value); }

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM|Blackboard")
	void BP_SetFloat(FName Key, float Value) { SetFloat(Key, Value); }

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM|Blackboard")
	void BP_SetVector(FName Key, FVector Value) { SetVector(Key, Value); }

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM|Blackboard")
	void BP_SetObject(FName Key, UObject* Value) { SetObject(Key, Value); }

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM|Blackboard")
	bool BP_ClearKey(FName Key) { return ClearKey(Key); }

	/** Drop every stored value across all typed maps. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM|Blackboard")
	void Reset();

	/** One-line dump of populated keys for the FSM debug string. */
	FString ToDebugString() const;

private:
	UPROPERTY()
	TMap<FName, bool> BoolValues;

	UPROPERTY()
	TMap<FName, int32> IntValues;

	UPROPERTY()
	TMap<FName, float> FloatValues;

	UPROPERTY()
	TMap<FName, FVector> VectorValues;

	/** Object refs are GC-visible (TObjectPtr) so a stored object stays alive while referenced. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UObject>> ObjectValues;
};
