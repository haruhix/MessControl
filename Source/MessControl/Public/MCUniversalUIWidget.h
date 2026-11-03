#pragma once

#include "MCGameplayUIWidget.h"
#include "Components/Button.h"
#include "MCUniversalUIWidget.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class EMCUniversalUIKind : uint8 { Panel, Button, Progress };

/** A reusable shape, not a screenshot: its Content slot accepts arbitrary UMG content. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUniversalUIWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUniversalUIWidget();
    virtual bool Initialize() override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Content") EMCUniversalUIKind Kind=EMCUniversalUIKind::Panel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Content") FName LabelKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Content",meta=(MultiLine=true)) FText Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Content") TObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Content",meta=(ClampMin="0",ClampMax="1")) float Progress=0.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Content") bool bSelected=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape") EMCUIColorToken Fill=EMCUIColorToken::Surface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape") EMCUIColorToken Foreground=EMCUIColorToken::OnMarine;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape") EMCUIColorToken ProgressFill=EMCUIColorToken::Mint;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape") bool bUseThemeShape=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape",meta=(ClampMin="0",EditCondition="!bUseThemeShape")) float CornerRadius=16.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape",meta=(ClampMin="0",EditCondition="!bUseThemeShape")) float OutlineWidth=3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape") FMargin ContentPadding=FMargin(16,12);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape") FVector2D ShadowOffset=FVector2D(0,5);
    /** Optional normalized simple polygon for a Panel; empty keeps the rounded shape. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape") TArray<FVector2D> ContourPoints;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape") bool bCustomColors=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape",meta=(EditCondition="bCustomColors")) FLinearColor CustomFill=FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape",meta=(EditCondition="bCustomColors")) FLinearColor CustomText=FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape",meta=(EditCondition="bCustomColors")) FLinearColor CustomOutline=FLinearColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Shape",meta=(EditCondition="bCustomColors")) FLinearColor CustomShadow=FLinearColor::Black;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Universal|Typography",meta=(ClampMin="8",ClampMax="96")) int32 FontSize=22;
    UPROPERTY(BlueprintAssignable, Category="Universal|Interaction") FOnButtonClickedEvent OnClicked;
    UFUNCTION(BlueprintCallable, Category="Universal") void SetLabel(FName Key,const FText& Fallback);
    UFUNCTION(BlueprintCallable, Category="Universal") void SetProgressValue(float Value);
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void GetSlotNames(TArray<FName>& SlotNames) const override;
private:
    UFUNCTION() void Clicked();
    TOptional<ESlateVisibility> PreContourShadowVisibility;
};

/** Responsive specimen sheet. It never reads or fabricates the player's gameplay state. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUniversalUIPreviewWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUniversalUIPreviewWidget();
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
};
