#pragma once

#include "CoreMinimal.h"
#include "MCVFXNativeInputs.generated.h"

class UNiagaraComponent;
class UNiagaraDataInterface;
class UNiagaraSystem;
class FMCFreezeCapturedGeometry;

/** Accepted mechanic data. These types never become artist overrides. */
UENUM()
enum class EMCVFXNativeInputType : uint8
{
    Float, Int, Bool, Color, Vector, Position, Quaternion,
    FloatArray, VectorArray, PositionArray
};

/** A tagged, reflected record: replication uses this array, never a replicated map.
 * Vector means an ordinary float vector. Position means a double world position
 * converted through Niagara's LWC position path, including position arrays. */
USTRUCT()
struct MESSCONTROL_API FMCVFXNativeInput
{
    GENERATED_BODY()

    UPROPERTY() FName Name;
    UPROPERTY() EMCVFXNativeInputType Type = EMCVFXNativeInputType::Float;
    UPROPERTY() float FloatValue = 0.f;
    UPROPERTY() int32 IntValue = 0;
    UPROPERTY() bool BoolValue = false;
    UPROPERTY() FLinearColor ColorValue = FLinearColor::White;
    UPROPERTY() FVector VectorValue = FVector::ZeroVector;
    UPROPERTY() FVector PositionValue = FVector::ZeroVector;
    UPROPERTY() FQuat QuaternionValue = FQuat::Identity;
    UPROPERTY() TArray<float> FloatValues;
    UPROPERTY() TArray<FVector> VectorValues;
    UPROPERTY() TArray<FVector> PositionValues;
};

USTRUCT()
struct MESSCONTROL_API FMCVFXNativeInputs
{
    GENERATED_BODY()

    UPROPERTY() TArray<FMCVFXNativeInput> Entries;

    static constexpr int32 MaxEntries = 64;
    static constexpr int32 MaxArrayValues = 1024;
    bool Validate(FString& Error) const;
    /** Every declared User.Native.* must have exactly typed data. Valid extra
     * payload entries are ignored, permitting one cue payload across its layers. */
    bool ValidateForSystem(const UNiagaraSystem* System, FString& Error,const FMCFreezeCapturedGeometry* FreezeCapture=nullptr) const;
    const FMCVFXNativeInput* Find(FName Name) const;
};

/** One cache per Niagara component. Reset before a new activation/reused component.
 * Not replicated: payloads remain the reflected typed array above. */
struct MESSCONTROL_API FMCVFXNativeInputsApplyState
{
    TWeakObjectPtr<UNiagaraComponent> Component;
    TWeakObjectPtr<UNiagaraSystem> System;
    FMCVFXNativeInputs Previous;
    TMap<FName,TWeakObjectPtr<UNiagaraDataInterface>> ArrayInterfaces;
    FVector LWCOrigin = FVector::ZeroVector;
    bool bValid = false;
    void Reset();
};

namespace MCVFXNativeInputs
{
    /** Reserved namespace classifier, including malformed empty suffixes. Match
     * FName's case-insensitive identity so display-case aliases cannot bypass it. */
    MESSCONTROL_API bool IsNativeName(FName Name);
    /** Game-thread only. Validate and resolve all interfaces before any writes.
     * Call before Activate and per tick after artist values. Static arrays upload
     * only when their value, interface, component/asset or LWC tile changes.
     * OutArrayUploads is optional observability, not a readiness certificate. */
    MESSCONTROL_API bool Apply(UNiagaraComponent* Component,const FMCVFXNativeInputs& Inputs,
        FString& Error,FMCVFXNativeInputsApplyState* State=nullptr,int32* OutArrayUploads=nullptr,
        const FMCFreezeCapturedGeometry* FreezeCapture=nullptr);
}
