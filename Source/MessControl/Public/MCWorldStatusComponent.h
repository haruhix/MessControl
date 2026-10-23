#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MCWorldStatusWidget.h"
#include "MCWorldStatusComponent.generated.h"

class UWidgetComponent;
class UTextRenderComponent;

/** Local presentation only. Replicated food/enemy state remains the sole data source. */
UCLASS(ClassGroup=(UI),meta=(BlueprintSpawnableComponent))
class MESSCONTROL_API UMCWorldStatusComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMCWorldStatusComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float Dt,ELevelTick Tick,FActorComponentTickFunction* Function) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status") bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status",meta=(ClampMin="100",ClampMax="10000")) float MaxDistance=2600.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status",meta=(ClampMin=".05",ClampMax="1")) float PollSeconds=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status") float HeightPadding=24.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Status") bool bOcclusionTest=true;
    UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="Status") TObjectPtr<UWidgetComponent> Screen;
    UPROPERTY(Transient,VisibleInstanceOnly,BlueprintReadOnly,Category="Status") TObjectPtr<UMCWorldStatusWidget> StatusWidget;
    UFUNCTION(BlueprintPure,Category="Status") bool SupportsOwner() const;
private:
    bool ReadSnapshot(FMCWorldStatusSnapshot& Value,FVector& Anchor) const;
    bool CanSee(FVector Anchor) const;
    void EnsureWidget();
    void HideLegacyLabel();
    double NextPollAt=0;
    FVector CurrentAnchor=FVector::ZeroVector;
    bool bSourceActive=false;
};
