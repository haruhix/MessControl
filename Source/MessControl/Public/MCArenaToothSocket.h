#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCArenaToothSocket.generated.h"
class UStaticMeshComponent;

/** Author a mouth tooth in the level. GameMode replaces the preview with its replicated tooth. */
UCLASS()
class MESSCONTROL_API AMCArenaToothSocket : public AActor
{
    GENERATED_BODY()
public:
    AMCArenaToothSocket();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Arena Tooth", meta=(ClampMin="1")) int32 ToothId=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Arena Tooth") TObjectPtr<UStaticMeshComponent> Preview;
};
