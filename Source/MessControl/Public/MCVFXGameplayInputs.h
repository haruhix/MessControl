#pragma once

#include "CoreMinimal.h"

class UNiagaraSystem;
class UNiagaraComponent;
class USceneComponent;

/** Exact, opt-in inputs supplied by the real presentation context, never artist overrides. */
namespace MCVFXGameplayInputs
{
    MESSCONTROL_API bool IsReadOnly(FName Name);
    MESSCONTROL_API bool ValidateSystem(const UNiagaraSystem* System,FString& Error);
    MESSCONTROL_API void ApplySubjectFrame(UNiagaraComponent* Component,const UNiagaraSystem* System,
        const USceneComponent* Subject,const FTransform& NiagaraFrame,const FVector& Direction=FVector::ZeroVector);
}
