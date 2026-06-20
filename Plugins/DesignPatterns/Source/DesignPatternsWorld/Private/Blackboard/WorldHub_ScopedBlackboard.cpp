// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Blackboard/WorldHub_ScopedBlackboard.h"
#include "Core/DPLog.h"

UWorldHub_ScopedBlackboard::UWorldHub_ScopedBlackboard()
{
	// Allocate the inner core blackboard up front so every forwarding accessor is always safe,
	// even before InitializeScopedBlackboard is called.
	EnsureCoreBlackboard();
}

void UWorldHub_ScopedBlackboard::EnsureCoreBlackboard()
{
	if (!CoreBlackboard)
	{
		// Instanced subobject of this scoped blackboard, so it is GC-owned and travels with us.
		CoreBlackboard = NewObject<UDP_Blackboard>(this, UDP_Blackboard::StaticClass(), TEXT("CoreBlackboard"));
	}
}

void UWorldHub_ScopedBlackboard::InitializeScopedBlackboard(const FWorldHub_Scope& InScope)
{
	Scope = InScope;
	EnsureCoreBlackboard();
}

void UWorldHub_ScopedBlackboard::SetChangeSink(UWorldHub_StateHubSubsystem* InSink)
{
	// Stored weakly and NOT as a UPROPERTY: the blackboard must never keep the subsystem alive.
	ChangeSinkWeak = InSink;
}

UWorldHub_StateHubSubsystem* UWorldHub_ScopedBlackboard::GetSink() const
{
	return ChangeSinkWeak.Get();
}

void UWorldHub_ScopedBlackboard::NotifyChanged(FName Key)
{
	if (UWorldHub_StateHubSubsystem* Sink = GetSink())
	{
		// Routed through the cross-area dispatch hook (defined by the subsystem area) so this
		// area never hard-depends on the subsystem's concrete type.
		WorldHub_DispatchScopedBlackboardChanged(Sink, Scope, Key);
	}
}

//~ Begin IDP_BlackboardProvider — faithful forwarding to CoreBlackboard.

bool UWorldHub_ScopedBlackboard::HasKey(FName Key) const
{
	return CoreBlackboard ? CoreBlackboard->HasKey(Key) : false;
}

EDP_BlackboardValueType UWorldHub_ScopedBlackboard::GetKeyType(FName Key) const
{
	return CoreBlackboard ? CoreBlackboard->GetKeyType(Key) : EDP_BlackboardValueType::None;
}

bool UWorldHub_ScopedBlackboard::GetBool(FName Key, bool bDefault) const
{
	return CoreBlackboard ? CoreBlackboard->GetBool(Key, bDefault) : bDefault;
}

int32 UWorldHub_ScopedBlackboard::GetInt(FName Key, int32 Default) const
{
	return CoreBlackboard ? CoreBlackboard->GetInt(Key, Default) : Default;
}

float UWorldHub_ScopedBlackboard::GetFloat(FName Key, float Default) const
{
	return CoreBlackboard ? CoreBlackboard->GetFloat(Key, Default) : Default;
}

FVector UWorldHub_ScopedBlackboard::GetVector(FName Key, const FVector& Default) const
{
	return CoreBlackboard ? CoreBlackboard->GetVector(Key, Default) : Default;
}

UObject* UWorldHub_ScopedBlackboard::GetObject(FName Key) const
{
	return CoreBlackboard ? CoreBlackboard->GetObject(Key) : nullptr;
}

void UWorldHub_ScopedBlackboard::SetBool(FName Key, bool bValue)
{
	EnsureCoreBlackboard();
	CoreBlackboard->SetBool(Key, bValue);
	NotifyChanged(Key);
}

void UWorldHub_ScopedBlackboard::SetInt(FName Key, int32 Value)
{
	EnsureCoreBlackboard();
	CoreBlackboard->SetInt(Key, Value);
	NotifyChanged(Key);
}

void UWorldHub_ScopedBlackboard::SetFloat(FName Key, float Value)
{
	EnsureCoreBlackboard();
	CoreBlackboard->SetFloat(Key, Value);
	NotifyChanged(Key);
}

void UWorldHub_ScopedBlackboard::SetVector(FName Key, const FVector& Value)
{
	EnsureCoreBlackboard();
	CoreBlackboard->SetVector(Key, Value);
	NotifyChanged(Key);
}

void UWorldHub_ScopedBlackboard::SetObject(FName Key, UObject* Value)
{
	EnsureCoreBlackboard();
	CoreBlackboard->SetObject(Key, Value);
	NotifyChanged(Key);
}

bool UWorldHub_ScopedBlackboard::ClearKey(FName Key)
{
	if (!CoreBlackboard)
	{
		return false;
	}

	const bool bRemoved = CoreBlackboard->ClearKey(Key);
	if (bRemoved)
	{
		NotifyChanged(Key);
	}
	return bRemoved;
}

//~ End IDP_BlackboardProvider

FString UWorldHub_ScopedBlackboard::ToDebugString() const
{
	const FString Inner = CoreBlackboard ? CoreBlackboard->ToDebugString() : TEXT("<null>");
	return FString::Printf(TEXT("ScopedBlackboard[%s] %s"), *Scope.ToString(), *Inner);
}
