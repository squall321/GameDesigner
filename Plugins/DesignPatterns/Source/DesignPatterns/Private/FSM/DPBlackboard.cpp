// Copyright DesignPatterns plugin. All Rights Reserved.

#include "FSM/DPBlackboard.h"

bool UDP_Blackboard::HasKey(FName Key) const
{
	return BoolValues.Contains(Key)
		|| IntValues.Contains(Key)
		|| FloatValues.Contains(Key)
		|| VectorValues.Contains(Key)
		|| ObjectValues.Contains(Key);
}

EDP_BlackboardValueType UDP_Blackboard::GetKeyType(FName Key) const
{
	if (BoolValues.Contains(Key))   { return EDP_BlackboardValueType::Bool; }
	if (IntValues.Contains(Key))    { return EDP_BlackboardValueType::Int; }
	if (FloatValues.Contains(Key))  { return EDP_BlackboardValueType::Float; }
	if (VectorValues.Contains(Key)) { return EDP_BlackboardValueType::Vector; }
	if (ObjectValues.Contains(Key)) { return EDP_BlackboardValueType::Object; }
	return EDP_BlackboardValueType::None;
}

bool UDP_Blackboard::GetBool(FName Key, bool bDefault) const
{
	const bool* Found = BoolValues.Find(Key);
	return Found ? *Found : bDefault;
}

int32 UDP_Blackboard::GetInt(FName Key, int32 Default) const
{
	const int32* Found = IntValues.Find(Key);
	return Found ? *Found : Default;
}

float UDP_Blackboard::GetFloat(FName Key, float Default) const
{
	const float* Found = FloatValues.Find(Key);
	return Found ? *Found : Default;
}

FVector UDP_Blackboard::GetVector(FName Key, const FVector& Default) const
{
	const FVector* Found = VectorValues.Find(Key);
	return Found ? *Found : Default;
}

UObject* UDP_Blackboard::GetObject(FName Key) const
{
	const TObjectPtr<UObject>* Found = ObjectValues.Find(Key);
	return Found ? Found->Get() : nullptr;
}

void UDP_Blackboard::SetBool(FName Key, bool bValue)
{
	BoolValues.Add(Key, bValue);
}

void UDP_Blackboard::SetInt(FName Key, int32 Value)
{
	IntValues.Add(Key, Value);
}

void UDP_Blackboard::SetFloat(FName Key, float Value)
{
	FloatValues.Add(Key, Value);
}

void UDP_Blackboard::SetVector(FName Key, const FVector& Value)
{
	VectorValues.Add(Key, Value);
}

void UDP_Blackboard::SetObject(FName Key, UObject* Value)
{
	ObjectValues.Add(Key, Value);
}

bool UDP_Blackboard::ClearKey(FName Key)
{
	int32 Removed = 0;
	Removed += BoolValues.Remove(Key);
	Removed += IntValues.Remove(Key);
	Removed += FloatValues.Remove(Key);
	Removed += VectorValues.Remove(Key);
	Removed += ObjectValues.Remove(Key);
	return Removed > 0;
}

void UDP_Blackboard::Reset()
{
	BoolValues.Reset();
	IntValues.Reset();
	FloatValues.Reset();
	VectorValues.Reset();
	ObjectValues.Reset();
}

FString UDP_Blackboard::ToDebugString() const
{
	const int32 Total = BoolValues.Num() + IntValues.Num() + FloatValues.Num()
		+ VectorValues.Num() + ObjectValues.Num();
	return FString::Printf(TEXT("BB[%d keys: %dB %dI %dF %dV %dO]"),
		Total, BoolValues.Num(), IntValues.Num(), FloatValues.Num(),
		VectorValues.Num(), ObjectValues.Num());
}
