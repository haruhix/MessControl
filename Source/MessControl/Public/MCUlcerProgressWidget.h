#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MCUlcerProgressWidget.generated.h"
class AMCMouthSurface;

/** Screen-space progress attached to the lesion, independently of the main HUD. */
UCLASS(Blueprintable)
class MESSCONTROL_API UMCUlcerProgressWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<AMCMouthSurface> Source;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") FLinearColor ProgressColor=FLinearColor(.25f,.94f,.74f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") float RingThickness=7;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
};
