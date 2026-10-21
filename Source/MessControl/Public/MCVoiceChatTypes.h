#pragma once

#include "CoreMinimal.h"
#include "MCVoiceChatTypes.generated.h"

UENUM(BlueprintType)
enum class EMCVoiceInputMode : uint8 { PushToTalk, OpenMic };

/** Current Windows input endpoints; OSS retains its default capture device until it is reinitialized. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCVoiceInputDevice
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly,Category="Voice") FString Id;
    UPROPERTY(BlueprintReadOnly,Category="Voice") FString Name;
    UPROPERTY(BlueprintReadOnly,Category="Voice") bool bDefault=false;
    UPROPERTY(BlueprintReadOnly,Category="Voice") bool bSelectable=false;
};
