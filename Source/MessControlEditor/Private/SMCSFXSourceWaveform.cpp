#include "SMCSFXSourceWaveform.h"

#include "MCComposerStyle.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElementTypes.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

void SMCSFXSourceWaveform::Construct(const FArguments& Args)
{OnSeek=Args._OnSeek;SetCursor(EMouseCursor::Crosshairs);SetClipping(EWidgetClipping::ClipToBounds);Reset();}

void SMCSFXSourceWaveform::Reset()
{
    Peaks.Reset();Seconds=0;StartFraction=0;
    Hint=FText::FromString(TEXT("▶ Слушать звук — построить волну WAV"));
    SetToolTipText(Hint);Invalidate(EInvalidateWidgetReason::Paint);
}

void SMCSFXSourceWaveform::ReadSource(USoundBase* Sound)
{
    const float PreviousStart=StartFraction;Reset();
    const auto* Wave=Cast<USoundWave>(Sound);
    if(!Wave || Wave->IsA<USoundWaveProcedural>())
    {Hint=FText::FromString(TEXT("Cue / MetaSound · открыть схему источника"));SetToolTipText(Hint);return;}
    if(!Wave->Transformations.IsEmpty())
    {Hint=FText::FromString(TEXT("WAV с преобразованиями · волна и seek в редакторе источника"));SetToolTipText(Hint);return;}
    constexpr int64 MaxPCMBytes=16ll*1024*1024;
    if(Wave->RawData.GetPayloadSize()<=0 || Wave->RawData.GetPayloadSize()>MaxPCMBytes)
    {Hint=FText::FromString(TEXT("Большой источник · волна в редакторе источника"));SetToolTipText(Hint);return;}
    TArray<uint8> PCM;uint32 Rate=0;uint16 Channels=0;
    if(!Wave->GetImportedSoundWaveData(PCM,Rate,Channels)||Rate==0||Channels==0||PCM.Num()>MaxPCMBytes)
    {Hint=FText::FromString(TEXT("Нет импортированных PCM-данных · открыть исходник"));SetToolTipText(Hint);return;}
    const int32 Frames=PCM.Num()/(sizeof(int16)*Channels);
    if(Frames<=0)return;
    Seconds=float(double(Frames)/Rate);
    StartFraction=PreviousStart;
    const int16* Samples=reinterpret_cast<const int16*>(PCM.GetData());
    const int32 Count=FMath::Min(512,Frames);Peaks.Reserve(Count);
    for(int32 Bucket=0;Bucket<Count;++Bucket)
    {
        float Min=0,Max=0;
        const int32 First=int32(int64(Bucket)*Frames/Count),Last=int32(int64(Bucket+1)*Frames/Count);
        for(int32 Frame=First;Frame<Last;++Frame)for(uint16 Channel=0;Channel<Channels;++Channel)
        {const float Value=float(Samples[Frame*Channels+Channel])/32768.f;Min=FMath::Min(Min,Value);Max=FMath::Max(Max,Value);}
        Peaks.Emplace(Min,Max);
    }
    Hint=FText::FromString(TEXT("Настоящая волна PCM · клик выбирает точку запуска; во время предпрослушки выполняет seek."));
    SetToolTipText(Hint);Invalidate(EInvalidateWidgetReason::Paint);
}

FReply SMCSFXSourceWaveform::OnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()!=EKeys::LeftMouseButton||Peaks.IsEmpty())return FReply::Unhandled();
    StartFraction=FMath::Clamp(float(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()).X/Geometry.GetLocalSize().X),0.f,.999f);
    OnSeek.ExecuteIfBound(GetStartSeconds());Invalidate(EInvalidateWidgetReason::Paint);return FReply::Handled();
}

int32 SMCSFXSourceWaveform::OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    using namespace MCComposerStyle;
    const FVector2D Size=Geometry.GetLocalSize();const auto* Brush=FAppStyle::GetBrush(TEXT("WhiteBrush"));
    FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),Brush,ESlateDrawEffect::None,GraphFill);
    if(Peaks.IsEmpty())
    {
        FSlateDrawElement::MakeText(Elements,Layer+1,Geometry.ToPaintGeometry(FVector2f(float(Size.X),float(Size.Y)),FSlateLayoutTransform(FVector2f(6,10))),Hint,
            FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10),ESlateDrawEffect::None,Muted);
        return Layer+1;
    }
    const float Middle=float(Size.Y)*.5f,Scale=FMath::Max(1.f,Middle-3),Width=float(Size.X)/Peaks.Num();
    for(int32 Index=0;Index<Peaks.Num();++Index)
    {
        const float Top=Middle-Peaks[Index].Y*Scale,Bottom=Middle-Peaks[Index].X*Scale;
        FSlateDrawElement::MakeBox(Elements,Layer+1,Geometry.ToPaintGeometry(FVector2f(FMath::Max(1.f,Width-.6f),FMath::Max(1.f,Bottom-Top)),FSlateLayoutTransform(FVector2f(Index*Width,Top))),
            Brush,ESlateDrawEffect::None,WorkspaceMint*Style.GetColorAndOpacityTint());
    }
    FSlateDrawElement::MakeBox(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2f(2,float(Size.Y)),FSlateLayoutTransform(FVector2f(StartFraction*float(Size.X),0))),Brush,ESlateDrawEffect::None,FLinearColor(.95f,.74f,.40f));
    return Layer+2;
}
