#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Styling/SlateTypes.h"
#include "MCGameplayUITheme.generated.h"

class UUserWidget;

UENUM(BlueprintType)
enum class EMCUIColorToken : uint8
{
    Surface,
    SurfaceAlt,
    Ink,
    Muted,
    Marine,
    Mint,
    Cream,
    Benefit,
    Cost,
    Border,
    Backdrop,
    Accent,
    OnMarine,
    Button,
    ButtonHover,
    ButtonPressed,
    ButtonDisabled,
    ButtonText,
    Focus,
    BossHealth,
    Shield,
    Warning,
    LCDBackground,
    LCDInk,
    Stamina,
    Cleanliness,
    BossSurface,
    BossRim,
    BossRimHighlight
};

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCUIThemePalette
{
    GENERATED_BODY()
    FMCUIThemePalette();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Surface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor SurfaceAlt;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Ink;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Muted;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Marine;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Mint;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Cream;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Benefit;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Cost;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Border;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Backdrop;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Accent;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor OnMarine;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Button;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor ButtonHover;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor ButtonPressed;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor ButtonDisabled;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor ButtonText;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Focus;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor BossHealth;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor BossSurface;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor BossRim;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor BossRimHighlight;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Shield;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Warning;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor LCDBackground;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor LCDInk;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Stamina;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette") FLinearColor Cleanliness;
};

DECLARE_MULTICAST_DELEGATE(FOnMCGameplayUIThemeChanged);

UCLASS(BlueprintType)
class MESSCONTROL_API UMCGameplayUITheme : public UDataAsset
{
    GENERATED_BODY()
public:
    UMCGameplayUITheme();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme") FMCUIThemePalette Palette;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Typography") FSlateFontInfo BodyFont;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Typography") FSlateFontInfo HeadingFont;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Typography") FSlateFontInfo DisplayFont;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Shape", meta=(ClampMin="0",ClampMax="32")) float CornerRadius=8.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Shape", meta=(ClampMin="0",ClampMax="4")) float OutlineWidth=1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Data") TSoftObjectPtr<class UDataTable> LocalizationTable;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Data") TSoftObjectPtr<class UDataTable> BoosterTable;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Data") TSoftObjectPtr<class UDataTable> CardTable;
    /** Shared authored gameplay widgets. Every resolved class must derive from its native role. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Widgets") TMap<FName,TSoftClassPtr<UUserWidget>> WidgetClasses;
    /** Portrait art only; gameplay names and health values remain localized UMG. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Theme|Art") TMap<FName,TSoftObjectPtr<class UTexture2D>> BossPortraits;
    UFUNCTION(BlueprintPure, Category="Theme") FLinearColor GetColor(EMCUIColorToken Token) const;
    UFUNCTION(BlueprintPure, Category="Theme") FSlateFontInfo MakeFont(int32 Size,bool bBold=true) const;
    UFUNCTION(BlueprintCallable, Category="Theme") void NotifyThemeChanged();
    FOnMCGameplayUIThemeChanged OnThemeChanged;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
#endif
};

