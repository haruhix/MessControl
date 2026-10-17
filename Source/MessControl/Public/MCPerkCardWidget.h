#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCPerkTypes.h"
#include "MCUIDataTypes.h"
#include "Styling/SlateTypes.h"
#include "MCPerkCardWidget.generated.h"

class AMCPlayerController;
class UBorder;
class UImage;
class UMCPerkCardButton;
class URichTextBlock;
class USizeBox;
class UTextBlock;
class UDataTable;
class UMCHUDIcon;
class UScrollBox;
struct FStreamableHandle;

/** Value-only presentation. A preview never owns or grants a gameplay perk. */
USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCPerkCardPresentation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card") FName GameplayID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card") FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card", meta=(MultiLine=true)) FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card") FText Badge;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card") FText StackPreview;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card") TSoftObjectPtr<UTexture2D> Image;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card") EMCPerkPolarity Polarity=EMCPerkPolarity::Positive;
    /** Authored numeric fragments, classified from the modifier stat rather than +/- alone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card") TArray<FString> BenefitTokens;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Card") TArray<FString> CostTokens;
};

/** The complete card is the action; there is no separate choose/selected footer. */
UCLASS()
class MESSCONTROL_API UMCPerkCardWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    void Configure(const FMCPerkCardPresentation& Data, AMCPlayerController* Controller, int32 ChoiceIndex, bool bInteractive=true);
    void SetInteractionEnabled(bool bEnabled);
    UMCPerkCardButton* GetChoiceButton() const { return ChoiceButton; }
    FButtonStyle GetBaseButtonStyle() const;
    /** Scroll readable copy without changing the selected or granted perk. */
    void ScrollContent(float Delta);
    /** Common value-only metadata for runtime cards and the selected-row designer fixture. */
    static void ApplyDefinitionMetadata(FMCPerkCardPresentation& Data,const FMCPerkDefinition& Definition,
        UDataTable* Localization,const FString& Culture,bool bPersonal,int32 Before);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Card") FMCPerkCardPresentation SampleCard;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Card", meta=(RowType="/Script/MessControl.MCUICardRow")) FDataTableRowHandle PreviewCardRow;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Preview|Card") bool bPreviewRowFound=false;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Preview|Card") bool bPreviewUsedFallback=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview|Card") bool bPreviewReadOnly=false;
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual void ResetEditorPreview() override;

protected:
    virtual void NativeDestruct() override;

private:
    void RefreshPresentation();
    void BindPresentationTree();
    void UpdateImage();
    void ShowLoadedImage(UTexture2D* Texture);
    UPROPERTY(Transient) TObjectPtr<UMCPerkCardButton> ChoiceButton;
    UPROPERTY(Transient) TObjectPtr<UBorder> BadgeBorder;
    UPROPERTY(Transient) TObjectPtr<UBorder> StatsBorder;
    UPROPERTY(Transient) TObjectPtr<UImage> Illustration;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> IconFallback;
    UPROPERTY(Transient) TObjectPtr<UMCHUDIcon> NativeIcon;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> BodyScroll;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BadgeLabel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatsLabel;
    UPROPERTY(Transient) TObjectPtr<URichTextBlock> DescriptionLabel;
    UPROPERTY(Transient) TObjectPtr<UDataTable> RichStyles;
    UPROPERTY(Transient) FMCPerkCardPresentation Presentation;
    UPROPERTY(Transient) FMCPerkCardPresentation LastPreviewSample;
    UPROPERTY(Transient) FDataTableRowHandle LastPreviewCardRow;
    UPROPERTY(Transient) TObjectPtr<UDataTable> PreviewDefinitions;
    /** Own every state brush while gamepad focus temporarily substitutes Normal with Hovered. */
    UPROPERTY(Transient) FButtonStyle BaseButtonStyle;
    TSoftObjectPtr<UTexture2D> RequestedImage;
    TSharedPtr<FStreamableHandle> ImageLoadHandle;
    bool bImageRequestFinished=false;
    bool bApplyingPreview=false;
    bool bInteractive=true;
    bool bHasExternalPresentation=false;
    bool bHasPreviewFixture=false;
    bool bLastPreviewReadOnly=false;
    FString LastPreviewCulture;
    TWeakObjectPtr<UDataTable> LastPreviewCardTable;
    TWeakObjectPtr<UDataTable> LastPreviewCopyTable;
};
