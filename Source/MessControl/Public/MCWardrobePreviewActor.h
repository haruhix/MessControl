#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MCCosmeticCatalog.h"
#include "MCWardrobePreviewActor.generated.h"

class AMCToothCharacter;
class APlayerController;
class UPoseableMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UUserWidget;
struct FMinimalViewInfo;

/** One private, pooled fitting-room display. This is deliberately an Actor, never a gameplay Pawn. */
UCLASS(NotPlaceable, Transient)
class MESSCONTROL_API AMCWardrobePreviewActor : public AActor
{
    GENERATED_BODY()
public:
    AMCWardrobePreviewActor();
    /** Called while the Hub is loading: retain all known mesh/material variants before Ready. */
    bool Prepare(APlayerController* LocalController, AMCToothCharacter* Source);
    bool BeginPresentation(const AActor* Booth);
    void EndPresentation();
    bool IsPreparedFor(const AMCToothCharacter* Source) const;
    bool IsPresenting() const { return bPresenting; }
    void SetDraftColor(FLinearColor Color);
    void SetDraftLoadout(const FMCCharacterCosmeticLoadout& Loadout);
    virtual void Tick(float DeltaSeconds) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Wardrobe") TObjectPtr<UPoseableMeshComponent> Body;
    UPROPERTY(EditAnywhere, Category="Wardrobe|Camera", meta=(ClampMin="25", ClampMax="60")) float HorizontalFieldOfView=38.f;
    UPROPERTY(EditAnywhere, Category="Wardrobe|Camera", meta=(ClampMin="0.2", ClampMax="0.4")) float PreviewScreenX=.28f;
    UPROPERTY(EditAnywhere, Category="Wardrobe|Camera", meta=(ClampMin="0.4", ClampMax="0.8")) float PreviewHeightFraction=.67f;

private:
    void CopyDisplayPose();
    void HideRealPlayersForThisView();
    void HideActorForThisView(AActor* Actor);
    void RestoreViewVisibility();

    UPROPERTY(Transient) TMap<FName,TObjectPtr<UStaticMeshComponent>> AccessoryParts;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BagMaterial;
    TWeakObjectPtr<APlayerController> LocalController;
    TWeakObjectPtr<AMCToothCharacter> SourceCharacter;
    TWeakObjectPtr<AActor> PreviousViewTarget;
    TArray<TWeakObjectPtr<AActor>> AddedHiddenActors;
    TMap<TWeakObjectPtr<UUserWidget>,float> HiddenWidgetOpacity;
    bool bPreviousAutoManageCamera=true;
    bool bPresenting=false;
    bool bSettledPoseCopied=false;
    float PoseSettleAge=0.f;
    float VisibilityRefreshAge=0.f;
    FVector CameraFront=FVector::ForwardVector;
};
