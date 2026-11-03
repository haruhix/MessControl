#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MCVFXPresentationLibrary.generated.h"

class UMCPerkVFXProfile;

/** Finite reflected presentation root for a location. Source IDs are stable gameplay identities. */
UCLASS(BlueprintType)
class MESSCONTROL_API UMCVFXPresentationLibrary : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Presentation")
    TMap<FName,TSoftObjectPtr<UMCPerkVFXProfile>> Profiles;
    static FSoftObjectPath MouthLibraryPath();
};
