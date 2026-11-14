#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class USoundBase;
DECLARE_DELEGATE_OneParam(FMCSeekSourceAudio,float);

/** Read-only peak envelope of the imported PCM, prepared only by explicit source playback. */
class SMCSFXSourceWaveform final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SMCSFXSourceWaveform){} SLATE_EVENT(FMCSeekSourceAudio,OnSeek) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void Reset();
    void ReadSource(USoundBase* Sound);
    float GetDuration() const {return Seconds;}
    float GetStartSeconds() const {return StartFraction*Seconds;}
    int32 GetPeakCount() const {return Peaks.Num();}
    virtual FVector2D ComputeDesiredSize(float) const override {return FVector2D(260,38);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool) const override;
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override;
private:
    TArray<FVector2f> Peaks;
    float Seconds=0,StartFraction=0;
    FText Hint;
    FMCSeekSourceAudio OnSeek;
};
