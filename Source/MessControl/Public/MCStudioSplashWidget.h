#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MCStudioSplashWidget.generated.h"

class UImage;
class UTextBlock;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

/** Application session state: returning from a hub/game never replays the studio ident. */
UCLASS()
class MESSCONTROL_API UMCStudioSplashSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    bool ClaimStartupPresentation();
private:
    bool bPresented=false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMCStudioSplashFinished);

/** Editable layered studio ident. The native timeline also supports deterministic Designer scrubbing. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCStudioSplashWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCStudioSplashWidget(const FObjectInitializer& Initializer);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Art") TObjectPtr<UTexture2D> WordmarkTexture;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Art") TObjectPtr<UTexture2D> BranchTexture;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Art") TObjectPtr<UTexture2D> LeavesTexture;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Art") TObjectPtr<UMaterialInterface> RevealMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Art") FLinearColor LogoColor=FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Art") FLinearColor BackgroundColor=FLinearColor(.002f,.002f,.002f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Timing",meta=(ClampMin=".5",ClampMax="10")) float Duration=3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Timing") bool bUseNativeTimeline=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Timing",meta=(ClampMin="0")) float WordRevealStart=.10f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Timing",meta=(ClampMin=".01")) float WordRevealEnd=.52f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Timing",meta=(ClampMin="0")) float BranchRevealStart=.29f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Timing",meta=(ClampMin=".01")) float BranchRevealEnd=.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Timing",meta=(ClampMin="0")) float LeavesRevealStart=.43f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Timing",meta=(ClampMin=".01")) float LeavesRevealEnd=.73f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Studio|Timing",meta=(ClampMin="0",ClampMax="1")) float FadeOutSeconds=.37f;
    UPROPERTY(BlueprintAssignable, Category="Studio") FMCStudioSplashFinished OnFinished;
    UFUNCTION(BlueprintCallable, Category="Studio") void SetTimelineTime(float Time);
    UFUNCTION(BlueprintPure, Category="Studio") bool IsFinished() const {return bFinished;}
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;
    /** Shared bounded easing for scrubbing/runtime; invalid time cannot poison UI transforms. */
    static float RevealAt(float Time,float Start,float End);
    static float CubicRevealAt(float Time,float Start,float End);
protected:
    virtual void SynchronizeProperties() override;
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnKeyDown(const FGeometry&,const FKeyEvent&) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry&,const FPointerEvent&) override;
private:
    void BindPresentation();
    void ApplyStaticPose();
    void ConfigureLayer(UImage* Image,UTexture2D* Texture,TObjectPtr<UMaterialInstanceDynamic>& Material);
    UPROPERTY(Transient) TObjectPtr<UImage> Wordmark;
    UPROPERTY(Transient) TObjectPtr<UImage> Branch;
    UPROPERTY(Transient) TObjectPtr<UImage> Leaves;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Fallback;
    UPROPERTY(Transient) TObjectPtr<UWidget> Composition;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WordmarkMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BranchMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LeavesMID;
    TWeakObjectPtr<UWidget> BoundRoot;
    TWeakObjectPtr<UTexture2D> ConfiguredWordmark,ConfiguredBranch,ConfiguredLeaves;
    TWeakObjectPtr<UMaterialInterface> ConfiguredRevealMaterial;
    FLinearColor ConfiguredLogoColor=FLinearColor::Transparent;
    double StartedAt=-1.;
    bool bFinished=false;
};
