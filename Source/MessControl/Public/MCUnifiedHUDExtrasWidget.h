#pragma once

#include "MCGameplayUIWidget.h"
#include "MCGameplayHUD.h"
#include "MCUnifiedHUDExtrasWidget.generated.h"

class UMCUniversalUIWidget;
class UVerticalBox;
class UTextBlock;
class UWrapBox;
class USizeBox;

/** Supplemental PSD-family shapes. The existing HUD remains the sole gameplay/presentation-state owner. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedHUDExtrasWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedHUDExtrasWidget();
    void SetBackend(UMCGameplayHUD* Source);
    void PreparePresentation();
    bool IsPresentationPrepared() const {return bPrepared;}
    void RefreshFromBackend();
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="HUD|Layout",meta=(ClampMin="240")) float MaximumCenterWidth=760;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="HUD|Layout",meta=(ClampMin="180")) float MaximumSideWidth=440;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="HUD|Layout",meta=(ClampMin="48")) float MaximumPassiveHeight=140;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="HUD|Layout",meta=(ClampMin="320")) float MaximumCriticalWidth=960;
    /** Reserve primary tools, skills and the existing contextual hint below the supplemental flow. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="HUD|Layout",meta=(ClampMin="80")) float SupplementalBottomInset=276;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void OnLocalizationChanged() override;
private:
    UVerticalBox* AddGroup(FName Name,FVector2D Anchor,FVector2D Position,FVector2D Alignment,bool bBlue=false);
    UMCUniversalUIWidget* Shape(FName Name,bool bBlue=false);
    UTextBlock* Text(FName Name,int32 Size);
    void Present(const FMCGameplayHUDExtrasSnapshot& Snapshot);
    void PresentPreview(float Time);
    void UpdateLayout();
    void EnsureSupplementalFlow();
    void ClearShapeLabels();
    void UpdatePassives(const TArray<FText>& Names);
    void HideGroups();
    TWeakObjectPtr<UMCGameplayHUD> Backend;
    TArray<FText> LastPassiveNames;
    UPROPERTY(Transient) TArray<TObjectPtr<USizeBox>> PassiveRows;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PassiveLabels;
    FVector2D LastArea=FVector2D::ZeroVector;
    FString LastLayoutSignature;
    FText LastCriticalCopy;
    bool bLastCriticalVisible=false;
    bool bPrepared=false,bPreparing=false,bUpdatingLayout=false;
    float RefreshElapsed=0;
};
