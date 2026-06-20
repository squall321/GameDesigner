// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Registry/WorldHub_FlagTypes.h"

ESeam_NetValueType WorldHub_FlagTypeToNetValueType(EWorldHub_FlagValueType FlagType)
{
	switch (FlagType)
	{
	case EWorldHub_FlagValueType::Bool:    return ESeam_NetValueType::Bool;
	case EWorldHub_FlagValueType::Int:     return ESeam_NetValueType::Int;
	case EWorldHub_FlagValueType::Float:   return ESeam_NetValueType::Float;
	case EWorldHub_FlagValueType::Vector:  return ESeam_NetValueType::Vector;
	case EWorldHub_FlagValueType::Tag:     return ESeam_NetValueType::Tag;
	case EWorldHub_FlagValueType::Name:    return ESeam_NetValueType::Name;
	// A counter is an integer over the wire.
	case EWorldHub_FlagValueType::Counter: return ESeam_NetValueType::Int;
	// A struct kind has no net projection — it is local/save only.
	case EWorldHub_FlagValueType::Struct:
	default:                               return ESeam_NetValueType::None;
	}
}

bool WorldHub_IsReplicableFlagType(EWorldHub_FlagValueType FlagType)
{
	return WorldHub_FlagTypeToNetValueType(FlagType) != ESeam_NetValueType::None;
}
