#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCGameplayUITheme.h"
#include "MCGameplayUIWidget.generated.h"

class UDataTable;

UENUM(BlueprintType)
enum class EMCGameplayUIPreviewScenario : uint8
{
    Default, Compact, LongText, LowResources, Empty, OneBoss, TwoBosses, ThreeBosses,
    ActiveEffect, Armed, Pending, Disabled, Selected, Victory, Defeat, Waiting
};

UENUM(BlueprintType)
enum class EMCUIStyleTarget : uint8 { Background, Text, Image, Progress, Button };

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCUIStyleBinding
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Style") FName WidgetName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Style") EMCUIColorToken Color=EMCUIColorToken::Ink;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Style") EMCUIStyleTarget Target=EMCUIStyleTarget::Text;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Style") int32 FontSize=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Style") bool bBold=false;
};

UCLASS(Blueprintable)
class MESSCONTROL_API UMCGameplayUIWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme") TObjectPtr<UMCGameplayUITheme> Theme;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme") TArray<FMCUIStyleBinding> StyleBindings;
    /** Optional screen override. Otherwise inherit the parent's override, then the resolved theme's table. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Localization", meta=(RequiredAssetDataTags="RowStructure=/Script/MessControl.MCUILocalizationRow"))
    TObjectPtr<UDataTable> TranslationTable;
    /** TextBlock/RichTextBlock widget name -> stable table key. Use static labels; format dynamic text in its presenter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Localization") TMap<FName,FName> TextBindings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview") EMCGameplayUIPreviewScenario PreviewScenario=EMCGameplayUIPreviewScenario::Default;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview") FName PreviewState=TEXT("Default");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview") FString PreviewCulture=TEXT("ru");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Preview",meta=(ClampMin=".1",ClampMax="60")) float PreviewDuration=4.f;
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Preview") float PreviewTime=0.f;
    UFUNCTION(BlueprintCallable, Category="Presentation") virtual void BuildPresentationTree();
    UFUNCTION(BlueprintCallable, Category="Preview") virtual void ApplyEditorPreview(float Time);
    UFUNCTION(BlueprintCallable, Category="Preview") virtual void ResetEditorPreview();
    UFUNCTION(BlueprintCallable, Category="Theme") virtual void ApplyTheme();
    UFUNCTION(BlueprintPure, Category="Theme") UMCGameplayUITheme* GetResolvedTheme() const;
    UFUNCTION(BlueprintPure, Category="Localization") virtual UDataTable* GetResolvedTranslationTable() const;
    UFUNCTION(BlueprintCallable, Category="Localization") void SetTranslationTable(UDataTable* NewTable);
    /** Reapply static labels after changing bindings. Table edits and the game's culture also refresh them. */
    UFUNCTION(BlueprintCallable, Category="Localization") void RefreshLocalization();
    FText Localized(FName Key,const FText& Fallback) const;
    void BindStyle(FName Name,EMCUIColorToken Color,EMCUIStyleTarget Target=EMCUIStyleTarget::Text,int32 Size=0,bool bBold=false);
    /** Update analytic brush metrics after Slate has allotted the composed tree. */
    void RefreshPresentationGeometry();
protected:
    virtual void BeginDestroy() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    /** Presenters may refresh their current snapshot here; runtime refresh must not create preview fixtures. */
    virtual void OnLocalizationChanged();
private:
    void ObserveTheme();
    void ObserveLocalization();
    void UnobserveLocalization();
    void ApplyTextBindings();
    TWeakObjectPtr<UMCGameplayUITheme> ObservedTheme;
    FDelegateHandle ThemeHandle;
    UPROPERTY(Transient) TObjectPtr<UDataTable> ObservedTranslationTable;
    FDelegateHandle TranslationTableHandle;
    FDelegateHandle CultureHandle;
    TMap<TWeakObjectPtr<UWidget>,FText> OriginalLocalizedCopy;
    bool bRefreshingLocalization=false;
    float BrushMetricsElapsed=1.f;
};

UCLASS(Blueprintable)
class MESSCONTROL_API UMCUIThemePreviewWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    virtual void BuildPresentationTree() override;
    virtual void ApplyEditorPreview(float Time) override;
};

