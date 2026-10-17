#pragma once
#include "CoreMinimal.h"
#include "MCGameplayUIWidget.h"
#include "Components/WidgetComponent.h"
#include "MCPresentationTypes.h"
#include "MCPlayerNameComponent.generated.h"

class UTextBlock;
class UBorder;
class UMCHUDIcon;

UCLASS()
class MESSCONTROL_API UMCPlayerNameWidget : public UMCGameplayUIWidget
{
    GENERATED_BODY()
public:
    void SetPlayerName(const FString& Name);
    FString GetPlayerName() const;
    void SetPresentation(FLinearColor Color,bool bHost,EMCPlayerAlarm Alarm);
    virtual void BuildPresentationTree() override;
    virtual void ApplyTheme() override;
    virtual void ApplyEditorPreview(float Time) override;
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> AlarmText;
    UPROPERTY(Transient) TObjectPtr<UBorder> AlarmPanel;
    UPROPERTY(Transient) TObjectPtr<UBorder> NameBackground;
    UPROPERTY(Transient) TObjectPtr<UMCHUDIcon> PlayerIcon;
    FString PresentedName;
    FLinearColor PresentedColor=FLinearColor::White;
    bool bPresentedHost=false;
    EMCPlayerAlarm PresentedAlarm=EMCPlayerAlarm::None;
};

/** Displays the replicated PlayerState name above the animated head. */
UCLASS(ClassGroup=(UI),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCPlayerNameComponent : public UWidgetComponent
{
    GENERATED_BODY()
public:
    UMCPlayerNameComponent();
    virtual void TickComponent(float DeltaSeconds,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Player Name",meta=(ClampMin="0",Units="cm")) float HeightAboveHead=120.f;
    UFUNCTION(BlueprintPure,Category="Player Name") FString GetDisplayedName() const { return DisplayedName; }
protected:
    virtual void BeginPlay() override;
private:
    void RefreshName();
    FString DisplayedName;
};
