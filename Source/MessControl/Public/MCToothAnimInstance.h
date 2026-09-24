#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MCToothAnimInstance.generated.h"

/** Procedural local-bone pose, evaluated through a thread-safe proxy. Editable settings live on the character. */
UCLASS(Transient,Blueprintable)
class MESSCONTROL_API UMCToothAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};
