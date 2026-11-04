#pragma once

#include "MCGameplayUIWidget.h"
#include "MCWorldLabelWidget.generated.h"

class UMCUniversalUIWidget;
class USizeBox;
class UTextBlock;

/** One editable label form; source owners retain the actual text and gameplay conditions. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCWorldLabelWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCWorldLabelWidget();
    /** Reflected loading root. Runtime preparation only consumes an already resident asset. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel|Resources") TSoftObjectPtr<UMCGameplayUITheme> PresentationTheme;
    /** Stable keys authored on the shared role, resolved from the resident table before location readiness. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel|Resources") TArray<FName> PreparationKeys;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel|Typography",meta=(ClampMin="8",ClampMax="48")) int32 CaptionFontSize=22;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel|Layout",meta=(ClampMin="32")) float MinimumWidth=110.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="WorldLabel|Layout",meta=(ClampMin="64")) float MaximumWidth=480.f;
    /** Designer only. Runtime always copies the real TextRender FText. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview",meta=(MultiLine=true)) FText PreviewCaption;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview") FLinearColor PreviewTint=FLinearColor(.45f,1.f,.9f);

    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    virtual UDataTable* GetResolvedTranslationTable() const override;
    /** Safe before a first visible label. No actor lookup, loads or gameplay callbacks. */
    bool PreparePresentation();
    bool HasPreparedPresentation() const;
    /** Unscaled fixed-font line height. Perspective scaling belongs to the world presenter, never text fitting. */
    float GetPreparedLineHeight() const {return PreparedLineHeight;}
    void SetPresentation(const FText& ActualText,FLinearColor ActualTint);
    static bool AreThemeResourcesResident(const UMCGameplayUITheme* Candidate,const UDataTable* CopyOverride=nullptr);
protected:
    virtual void OnLocalizationChanged() override;
private:
    bool ResolveResidentTheme();
    void RefreshCaption();
    UPROPERTY(Transient) TObjectPtr<USizeBox> LabelBounds;
    UPROPERTY(Transient) TObjectPtr<UMCUniversalUIWidget> LabelFrame;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CaptionText;
    FText SourceCaption;
    FLinearColor SourceTint=FLinearColor::White;
    FString PreparedCaption,PreparedKeyCopy,PreparedCulture;
    TArray<FName> PreparedKeys;
    TWeakObjectPtr<UDataTable> PreparedCopyTable;
    FSlateFontInfo PreparedFont;
    float PreparedScale=0.f,PreparedMinimumWidth=0.f,PreparedMaximumWidth=0.f,PreparedLineHeight=0.f;
    FMargin PreparedInsets;
    bool bPrepared=false;
};
