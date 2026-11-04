#pragma once

#include "MCGameplayUIWidget.h"
#include "MCEmoteWidget.h"
#include "MCUnifiedEmoteWidget.generated.h"

class UMCUniversalUIWidget;
class UTextBlock;
class UMCUnifiedEmoteWidget;

/** Nonfocusable mouse proxy; source owner remains the keyboard/held-T input authority. */
UCLASS()
class MESSCONTROL_API UMCUnifiedEmoteChoiceButton : public UButton
{
    GENERATED_BODY()
public:
    UMCUnifiedEmoteChoiceButton();
};
UCLASS()
class MESSCONTROL_API UMCUnifiedEmoteBinding : public UObject
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<UMCUnifiedEmoteWidget> Owner;
    TWeakObjectPtr<UMCUniversalUIWidget> Frame;
    FMCEmotePresentationChoice Token;
    UFUNCTION() void Clicked();
    UFUNCTION() void Hovered();
};

/** New shape-only view of the one retained emote owner and its accepted input flow. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUnifiedEmoteWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    UMCUnifiedEmoteWidget();
    void SetBackend(UMCEmoteWidget* Source);
    void PreparePresentation();
    bool IsPresentationPrepared() const {return bPrepared;}
    bool HasRuntimePresentation() const {return Backend.IsValid() && LastSnapshot.bVisible && LastSnapshot.bHasGeometry;}
    void RefreshFromBackend();
    bool RouteSourceKeyDown(const FKeyEvent& Event);
    bool RouteSourceKeyUp(const FKeyEvent& Event);
    void ForwardChoice(const UMCUnifiedEmoteBinding* Binding,bool bHoverOnly);
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Emotes") TSoftObjectPtr<UMCEmoteLibrary> PreviewLibrary=TSoftObjectPtr<UMCEmoteLibrary>(FSoftObjectPath(TEXT("/Game/Data/DA_Emotes.DA_Emotes")));
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Emotes",meta=(ClampMin="0",ClampMax="2")) int32 PreviewCategory=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Preview|Emotes") bool bPreviewActionsAvailable=true;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyUp(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeOnFocusLost(const FFocusEvent& Event) override;
    virtual void OnLocalizationChanged() override;
private:
    UMCUniversalUIWidget* MakeShape(FName Name,FLinearColor Fill);
    UTextBlock* MakeLabel(FName Name,int32 Size);
    void Present(const FMCEmotePresentationSnapshot& Snapshot);
    void PresentPreview();
    void UpdateLayout();
    void UpdateChoices(const FMCEmotePresentationSnapshot& Snapshot);
    bool CanForward() const;
    TWeakObjectPtr<UMCEmoteWidget> Backend;
    FMCEmotePresentationSnapshot LastSnapshot;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUnifiedEmoteBinding>> Bindings;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUniversalUIWidget>> ChoiceViews;
    UPROPERTY(Transient) TArray<TObjectPtr<UMCUniversalUIWidget>> ColorViews;
    UPROPERTY(Transient) TObjectPtr<UMCEmoteLibrary> RetainedPreviewLibrary;
    FVector2D LastViewport=FVector2D::ZeroVector;
    FString PreparedCulture;
    float PreparedFontScale=0,ViewRefreshElapsed=0;
    bool bPrepared=false,bPreparing=false;
};
