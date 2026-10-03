#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCScoreboardWidget.generated.h"

class UVerticalBox;
class UTextBlock;
class UMCHUDIcon;

USTRUCT(BlueprintType)
struct FMCScoreboardEntry
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Nickname;
    UPROPERTY(BlueprintReadOnly) int32 Points=0;
    UPROPERTY(BlueprintReadOnly) float PingMilliseconds=0;
    UPROPERTY(BlueprintReadOnly) FLinearColor PlayerColor=FLinearColor::White;
    UPROPERTY(BlueprintReadOnly) bool bHost=false;
    UPROPERTY(BlueprintReadOnly) bool bSpectating=false;
};

USTRUCT()
struct FMCScoreboardRow
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Name;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Points;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Ping;
    UPROPERTY(Transient) TObjectPtr<UMCHUDIcon> Icon;
};

/** Tab overlay; reads replicated PlayerStates without changing gameplay input. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCScoreboardWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Scoreboard") void Refresh();
    UPROPERTY(BlueprintReadOnly,Category="Scoreboard") TArray<FMCScoreboardEntry> PlayerEntries;
    UFUNCTION(BlueprintImplementableEvent,Category="Scoreboard") void OnScoreboardUpdated();
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float Dt) override;
private:
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Players;
    UPROPERTY(Transient) TArray<FMCScoreboardRow> Rows;
    float RefreshElapsed=.25f;
    void RebuildRows(int32 Count);
};
