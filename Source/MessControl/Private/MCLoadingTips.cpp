#include "MCLoadingTips.h"

#include "MCUIDataLibrary.h"

namespace
{
struct FLoadingTipCopy {const TCHAR* Key;const TCHAR* RU;const TCHAR* EN;};
// Oral-care wording is adapted from NHS daily hygiene guidance and ADA/MouthHealthy
// brushing guidance; source URLs and the reviewed copy live in the authoring payload.
const FLoadingTipCopy Tips[]={
    {TEXT("loading.tip.care.brush_twice"),TEXT("Две минуты дважды в день — хороший ритм для чистки зубов."),TEXT("Make room for two minutes of brushing, twice a day.")},
    {TEXT("loading.tip.coop.roles"),TEXT("Разделите заботы: один режет еду, другой доставляет кусочки, третий убирает."),TEXT("Share the jobs: cut food, deliver pieces and clean up together.")},
    {TEXT("loading.tip.care.soft_brush"),TEXT("Мягкая щётка и аккуратные движения помогают бережно очистить зубы."),TEXT("Use a soft toothbrush and gentle strokes to clean with care.")},
    {TEXT("loading.tip.coop.freshness"),TEXT("Свежесть не ждёт. Доставляйте кусочки еды, пока они не испортились."),TEXT("Freshness won't wait. Deliver food pieces before they spoil.")},
    {TEXT("loading.tip.care.all_surfaces"),TEXT("Чистите зубы со всех сторон: снаружи, изнутри и там, где жуёте."),TEXT("Clean the outside, inside and chewing surfaces of every tooth.")},
    {TEXT("loading.tip.coop.reserve"),TEXT("Берегите зубы арены: это запас жизней вашей команды."),TEXT("Protect the arena teeth: they're your team's reserve lives.")},
    {TEXT("loading.tip.care.replace_brush"),TEXT("Щетинки распушились? Пора менять щётку. Обычно это нужно раз в 3–4 месяца."),TEXT("Frayed bristles? A fresh toothbrush is due—usually every three to four months.")},
    {TEXT("loading.tip.coop.ready"),TEXT("Перед новой вылазкой соберитесь всей командой в READY-зоне."),TEXT("Gather the whole team in the READY zone before the next run.")}
};
}

int32 MCLoadingTips::Count() {return UE_ARRAY_COUNT(Tips);}
FName MCLoadingTips::Key(int32 Index) {return Index>=0 && Index<Count()?FName(Tips[Index].Key):NAME_None;}
TArray<FText> MCLoadingTips::Resolve(UDataTable* Table,const FString& Culture)
{
    check(IsInGameThread());
    const bool Russian=Culture.StartsWith(TEXT("ru"),ESearchCase::IgnoreCase);
    TArray<FText> Result;Result.Reserve(Count());
    for(const auto& Tip:Tips)
        Result.Add(UMCUIDataLibrary::ResolveText(Table,Tip.Key,FText::AsCultureInvariant(Russian?Tip.RU:Tip.EN),Culture));
    return Result;
}
FText MCLoadingTips::Heading(UDataTable* Table,const FString& Culture)
{
    check(IsInGameThread());
    const bool Russian=Culture.StartsWith(TEXT("ru"),ESearchCase::IgnoreCase);
    return UMCUIDataLibrary::ResolveText(Table,TEXT("loading.tip.label"),
        FText::AsCultureInvariant(Russian?TEXT("Полезный совет"):TEXT("A little tip")),Culture);
}
FMCLoadingTipBlend MCLoadingTips::Evaluate(double Seconds,int32 TipCount)
{
    FMCLoadingTipBlend Result;
    if(TipCount<=0) return Result;
    Result.Current=0;Result.Next=TipCount>1?1:0;Result.CurrentOpacity=1.f;
    if(TipCount==1) return Result;
    const double Age=FMath::IsFinite(Seconds)?FMath::Max(0.,Seconds):0.;
    const double Cycle=HoldSeconds+FadeSeconds;
    const double WithinDeck=FMath::Fmod(Age,Cycle*TipCount);
    Result.Current=FMath::Clamp(FMath::FloorToInt(WithinDeck/Cycle),0,TipCount-1);
    Result.Next=(Result.Current+1)%TipCount;
    const double WithinTip=WithinDeck-Result.Current*Cycle;
    const float T=FMath::Clamp(float((WithinTip-HoldSeconds)/FadeSeconds),0.f,1.f);
    Result.NextOpacity=T*T*(3.f-2.f*T);
    Result.CurrentOpacity=1.f-Result.NextOpacity;
    return Result;
}
