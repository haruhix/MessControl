#pragma once
#include "CoreMinimal.h"
#include "Sound/SoundClass.h"
#include "MCVoiceSoundClass.generated.h"

/** Native VOIP root, also resolved by AudioSettings before the first remote packet. */
UCLASS()
class MESSCONTROL_API UMCVoiceSoundClass : public USoundClass
{
    GENERATED_BODY()
public:
    UMCVoiceSoundClass(const FObjectInitializer& Initializer);
};
