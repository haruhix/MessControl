#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MCPhysicsAssetBuilder.generated.h"
class USkeletalMesh;
class UPhysicsAsset;

UCLASS()
class MESSCONTROL_API UMCPhysicsAssetBuilder : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="MessControl|Editor") static UPhysicsAsset* BuildToothPhysicsAsset(USkeletalMesh* Mesh);
};
