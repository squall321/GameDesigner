// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FSM/DPBlackboard.h"
#include "Hub/WorldHub_Scope.h"
#include "WorldHub_ScopedBlackboard.generated.h"

// Forward-declared: the scoped blackboard reports changes to the hub subsystem through a
// non-owning back-pointer, without this module's blackboard area hard-depending on the
// subsystem area's full type.
class UWorldHub_StateHubSubsystem;

struct FWorldHub_Scope;

/**
 * Cross-area dispatch hook implemented by the subsystem area.
 *
 * The scoped-blackboard area only forward-declares UWorldHub_StateHubSubsystem, so it cannot call
 * a concrete method on it directly. Instead the subsystem area DEFINES this function (in its own
 * .cpp) to route a blackboard change into the hub's change pipeline. The blackboard area DECLARES
 * it here and calls it; if the subsystem area is not present the linker will flag the missing
 * definition, making the cross-area contract explicit.
 *
 * @param Sink  the live hub subsystem the change should be delivered to (never null at call site).
 * @param Scope the scope of the blackboard that changed.
 * @param Key   the FName key that changed within that scope.
 */
DESIGNPATTERNSWORLD_API void WorldHub_DispatchScopedBlackboardChanged(
	UWorldHub_StateHubSubsystem* Sink, const FWorldHub_Scope& Scope, FName Key);

/**
 * A per-scope key/value blackboard that bridges the core UDP_Blackboard into the world hub's
 * change pipeline.
 *
 * It implements IDP_BlackboardProvider FAITHFULLY by forwarding every read and write to an inner
 * UDP_Blackboard (FName keys; the core's Bool/Int/Float/Vector/Object channels — there is NO
 * struct channel, matching the core contract). On each mutating call it additionally notifies a
 * change sink (the hub subsystem) so hub-level listeners, replication mirrors and saves can react
 * to blackboard edits, tagged with the scope this blackboard belongs to.
 *
 * The sink is a NON-UPROPERTY weak back-pointer: a child object must not keep the subsystem alive,
 * and the subsystem out-lives every blackboard it owns. The inner core blackboard, by contrast, IS
 * a UPROPERTY TObjectPtr because this object owns it and must keep it GC-alive.
 *
 * This is LOCAL working memory: it is not itself replicated. Authoritative, save-bearing state
 * lives in the flag registry; the change sink is how a hub turns selected blackboard edits into
 * hub-level (and, if the hub chooses, replicated) effects.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSWORLD_API UWorldHub_ScopedBlackboard : public UObject, public IDP_BlackboardProvider
{
	GENERATED_BODY()

public:
	UWorldHub_ScopedBlackboard();

	/**
	 * The scope this blackboard's values belong to. Set once at creation by the owning hub and
	 * carried into every change notification so the sink can route per-scope.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Blackboard")
	FWorldHub_Scope Scope;

	/** Initialize the inner core blackboard (creating it if needed) and record the owning scope. */
	void InitializeScopedBlackboard(const FWorldHub_Scope& InScope);

	/**
	 * Wire (or clear) the change sink. Stored as a raw weak back-pointer — NOT a UPROPERTY — so the
	 * blackboard never keeps the subsystem alive. Pass nullptr to detach.
	 */
	void SetChangeSink(UWorldHub_StateHubSubsystem* InSink);

	/** @return the inner core blackboard this scoped view forwards to (never null after init). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|Blackboard")
	UDP_Blackboard* GetCoreBlackboard() const { return CoreBlackboard; }

	//~ Begin IDP_BlackboardProvider (faithful forwarding to CoreBlackboard)
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

	/** One-line dump for debug strings (scope + inner blackboard contents). */
	FString ToDebugString() const;

private:
	/**
	 * The owned, GC-visible core blackboard that actually stores values. Created in the ctor and
	 * (re)ensured in InitializeScopedBlackboard so reads/writes are always safe to forward.
	 */
	UPROPERTY()
	TObjectPtr<UDP_Blackboard> CoreBlackboard;

	/**
	 * Non-owning back-pointer to the change sink. Intentionally NOT a UPROPERTY (must not keep the
	 * subsystem alive); always null-checked before use via GetSink().
	 */
	TWeakObjectPtr<UWorldHub_StateHubSubsystem> ChangeSinkWeak;

	/** Ensure CoreBlackboard exists, allocating it as an instanced subobject of this object. */
	void EnsureCoreBlackboard();

	/** Resolve the sink if it is still alive, else nullptr. */
	UWorldHub_StateHubSubsystem* GetSink() const;

	/** Notify the sink (if any) that Key changed in this blackboard's scope. */
	void NotifyChanged(FName Key);
};
