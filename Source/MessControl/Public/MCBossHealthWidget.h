#pragma once

#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "MCBossHealthWidget.generated.h"

class UCanvasPanel;
class UProgressBar;
class UTextBlock;
class UBorder;
class UVerticalBox;
class UImage;
class UHorizontalBox;
class UTexture2D;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct MESSCONTROL_API FMCBossHealthPresentation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName BossID;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FText Name;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float Health=100.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaxHealth=100.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bHasShield=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float Shield=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaxShield=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<UTexture2D> Portrait;
};

USTRUCT()
struct FMCBossHealthRow
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UBorder> Panel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Name;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Value;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> Current;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> Loss;
    UPROPERTY(Transient) TObjectPtr<UBorder> ShieldPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ShieldValue;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> ShieldBar;
    UPROPERTY(Transient) TObjectPtr<UImage> Portrait;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PortraitFallback;
    UPROPERTY(Transient) TObjectPtr<UImage> Frame;
    UPROPERTY(Transient) TObjectPtr<UImage> ShieldIcon;
    UPROPERTY(Transient) TObjectPtr<UImage> Tail;
    FName BossID;
    float LastFraction=1.f,DelayedFraction=1.f,LossHold=0.f;
    float VerticalOffset=0.f;
};

/** Local, non-interactive encounter UI. Gameplay health remains in the boss's replicated snapshot. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCBossHealthWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    /** UI-domain LED mask; vertex tint comes from the semantic theme palette. Empty uses a plain fill. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance")
    TSoftObjectPtr<UMaterialInterface> LEDFillMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Gameplay/UI/Materials/M_UI_LEDGrid.M_UI_LEDGrid")));
    /** Shared envelope for every boss portrait, independent of the source PNG size. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance",meta=(ClampMin="16",ClampMax="128"))
    FVector2D PortraitBoxSize=FVector2D(73.45f,73.45f);
    /** Optional optical compensation for transparent padding; 1 keeps the fitted image size. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance")
    TMap<FName,float> PortraitScaleByBoss;
    /** Procedural union of the body and the inserted portrait's alpha silhouette. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance")
    TSoftObjectPtr<UMaterialInterface> FrameMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance")
    TSoftObjectPtr<UMaterialInterface> ShieldIconMaterial;
    /** Separate fonts keep all boss names, percentages and translations live. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance|Typography")
    FSlateFontInfo BossNameFont;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance|Typography")
    FSlateFontInfo BossValueFont;
    /** Maximum logical width; viewport DPI and corner HUD safe space are applied separately. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance|Layout",meta=(ClampMin="816",ClampMax="1440"))
    float MaximumStackWidth=1080.f;
    void ShowHealth(float Health, float MaxHealth, float DeltaSeconds);
    void HideHealth();
    void SetLetterbox(bool bShow);
    UFUNCTION(BlueprintCallable,Category="HUD|Boss") void ShowBosses(const TArray<FMCBossHealthPresentation>& Bosses,float DeltaSeconds);
    UFUNCTION(BlueprintPure,Category="HUD|Boss") int32 GetDisplayedBossCount() const {return bLetterbox?0:ActiveBosses.Num();}
    /** Includes the portrait/frame overhang, relative to the stack's top anchor. */
    UFUNCTION(BlueprintPure,Category="HUD|Boss") float GetDisplayedStackHeight() const {return bLetterbox || !bShowingHealth?0.f:DisplayedStackHeight;}
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
private:
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> HealthPanel;
    UPROPERTY(Transient) TArray<FMCBossHealthRow> Rows;
    UPROPERTY(Transient) TArray<FMCBossHealthPresentation> ActiveBosses;
    UPROPERTY(Transient) TObjectPtr<UBorder> TopBar;
    UPROPERTY(Transient) TObjectPtr<UBorder> BottomBar;
    bool bShowingHealth=false;
    bool bLetterbox=false;
    float LayoutViewportWidth=1920.f;
    float DisplayedStackHeight=0.f;
    float CachedWidthLimit=-1.f;
    FString CachedLayoutNames;
    FSlateFontInfo CachedLayoutFont;
    void UpdateResponsiveLayout();
    void PresentBosses(float DeltaSeconds);
};
