#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "MCUIContourShape.generated.h"

class SMCUIContourShape;

/** Editable simple polygon panel. Vertices use the widget's normalized [0,1] area. */
UCLASS()
class MESSCONTROL_API UMCUIContourShape : public UWidget
{
    GENERATED_BODY()
public:
    /** 3-64 clockwise or counterclockwise vertices; concave is supported, self intersections are not. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contour") TArray<FVector2D> Points;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contour") FLinearColor FillColor=FLinearColor::White;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contour") FLinearColor OutlineColor=FLinearColor::Black;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contour") FLinearColor ShadowColor=FLinearColor(0,0,0,.35f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contour",meta=(ClampMin="0")) float OutlineWidth=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contour",meta=(ClampMin="0")) float CornerRadius=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contour") FVector2D ShadowOffset=FVector2D(0,5);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Contour") FVector2D DesiredSize=FVector2D(100,100);
    virtual void SynchronizeProperties() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    TSharedPtr<SMCUIContourShape> ContourSlate;
};
