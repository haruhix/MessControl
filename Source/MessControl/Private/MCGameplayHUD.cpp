#include "MCGameplayHUD.h"
#include "MCStaminaWidget.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCSingleDayDirector.h"
#include "MCProgressionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCGripComponent.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCMouthSurface.h"
#include "MCArenaTooth.h"
#include "MCThroat.h"
#include "MCCoffeeFlood.h"
#include "MCFogBrawlEvent.h"
#include "MCRewardChest.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "HAL/IConsoleManager.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/GameStateBase.h"
#include "Widgets/SWidget.h"

// Unity builds combine this file with the prototype widgets and mouth renderer.
namespace MCGameplayHUDPrivate
{
TAutoConsoleVariable<int32> DirectorDebug(TEXT("MC.Director.Debug"),0,
    TEXT("1: show Director observations, candidate weights/probabilities and recent decisions; 0: compact monitor."),ECVF_Default);
const FLinearColor Ink(.012f,.020f,.033f,.90f),White(.98f,.97f,.93f),Muted(.56f,.66f,.72f),Mint(.26f,.91f,.71f),Amber(1.f,.64f,.23f),Blue(.22f,.65f,1.f);
FString PacingName(EMCGameDirectorPacing Pacing)
{
    switch(Pacing) {
    case EMCGameDirectorPacing::Build:return TEXT("НАРАСТАНИЕ");
    case EMCGameDirectorPacing::Drain:return TEXT("РАЗГРУЗКА");
    case EMCGameDirectorPacing::Rest:return TEXT("ПЕРЕДЫШКА");
    case EMCGameDirectorPacing::FinalCleanup:return TEXT("ФИНАЛЬНАЯ УБОРКА");
    default:return TEXT("ПЕРЕРЫВ"); }
}
FString ShortLine(FString Value,int32 Limit)
{
    Value.ReplaceInline(TEXT("\r"),TEXT(" "));Value.ReplaceInline(TEXT("\n"),TEXT(" "));
    return Value.Len()>Limit?Value.Left(Limit-1)+TEXT("…"):Value;
}
FString CandidateName(EMCGameDirectorEvent Kind)
{
    switch(Kind) {
    case EMCGameDirectorEvent::Food:return TEXT("Еда");
    case EMCGameDirectorEvent::Coffee:return TEXT("Пятна кофе");
    case EMCGameDirectorEvent::CoffeeFlood:return TEXT("Река кофе");
    case EMCGameDirectorEvent::ColdCola:return TEXT("Кола");
    case EMCGameDirectorEvent::Yawn:return TEXT("Зевание");
    case EMCGameDirectorEvent::Pepper:return TEXT("Перец");
    case EMCGameDirectorEvent::StuckFood:return TEXT("Застр. еда");
    case EMCGameDirectorEvent::LooseTooth:return TEXT("Шаткий зуб");
    case EMCGameDirectorEvent::Reward:return TEXT("Сундук");
    default:return TEXT("Босс"); }
}
float CandidateProbability(const FMCGameDirectorCandidate& Candidate)
{
    return Candidate.EffectiveWeight>0?FMath::Clamp(Candidate.Probability,0.f,1.f):0.f;
}
int32 Percent(float Value) { return FMath::RoundToInt(FMath::Clamp(Value,0.f,1.f)*100); }
FString StepName(EMCDayStep Step)
{
    switch(Step) {
    case EMCDayStep::BrushLesson:return TEXT("ВРЕМЯ ЧИСТИТЬ");
    case EMCDayStep::DiscardBrushes:return TEXT("ЩЁТКИ ЗА БОРТ");
    case EMCDayStep::BreakfastRain:return TEXT("ЗАВТРАК ПАДАЕТ");
    case EMCDayStep::BreakfastCleanup:return TEXT("УБРАТЬ ОСТАТКИ");
    case EMCDayStep::CoffeeWaves:return TEXT("ЦУНАМИ");
    case EMCDayStep::CoffeeCleanup:return TEXT("СМЫТЬ КОФЕ");
    case EMCDayStep::ColdCola:return TEXT("ХОЛОДНАЯ КОЛА");
    case EMCDayStep::StuckFood:return TEXT("МЕЖДУ ЗУБАМИ");
    default:return TEXT("ДЕНЬ ЗАВЕРШЁН"); }
}
struct FHUDPainter
{
    const FGeometry& G; FSlateWindowElementList& E; int32 L; float S;
    void Box(float X,float Y,float W,float H,FLinearColor C,float Radius=12) const {
        if(W<=0 || H<=0) return;
        const FSlateRoundedBoxBrush Brush(FLinearColor::White,Radius*S);
        FSlateDrawElement::MakeBox(E,L,G.ToPaintGeometry(FVector2D(W*S,H*S),FSlateLayoutTransform(FVector2D(X*S,Y*S))),&Brush,ESlateDrawEffect::None,C);
    }
    void Text(float X,float Y,const FString& T,int32 Size,FLinearColor C=White) const {
        FSlateDrawElement::MakeText(E,L+1,G.ToPaintGeometry(FVector2D(1000*S,80*S),FSlateLayoutTransform(FVector2D(X*S,Y*S))),T,FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),FMath::Max(8,FMath::RoundToInt(Size*S))),ESlateDrawEffect::None,C);
    }
    void Line(TArray<FVector2D> Points,FLinearColor C=White,float Thickness=3) const {
        for(auto& P:Points) P*=S;
        FSlateDrawElement::MakeLines(E,L+2,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,C,true,Thickness*S);
    }
    void Bar(float X,float Y,float W,float H,float Value,FLinearColor C) const {
        Box(X,Y,W,H,FLinearColor(.004f,.008f,.013f,.95f),H/2);
        Box(X+2,Y+2,(W-4)*FMath::Clamp(Value,0.f,1.f),H-4,C,(H-4)/2);
    }
    void Tool(float X,float Y,int32 Slot,FLinearColor C=White,float Size=1) const {
        auto Path=[&](std::initializer_list<FVector2D> P,float Width=4) { TArray<FVector2D> V; for(auto Q:P) V.Add(FVector2D(X,Y)+Q*Size); Line(V,C,Width*Size); };
        if(Slot==0) {
            Path({{-13,20},{9,-17},{18,-11},{-5,24},{-13,20}},4);
            for(int I=0;I<4;++I) Path({{7.f+I*3,-14.f+I*2},{12.f+I*3,-23.f+I*2}},3);
        } else if(Slot==1) {
            Path({{-14,22},{9,-13}},7); Path({{-22,-8},{-13,-17},{1,-21},{17,-15},{25,-5}},6);
        } else if(Slot==2) {
            Path({{-16,23},{-4,6}},7); Path({{-4,6},{14,-25},{20,-8},{5,10},{-4,6}},4);
        } else {
            Box(X-13*Size,Y-4*Size,26*Size,30*Size,C,5*Size);
            Path({{-6,-6},{-6,-16},{9,-16},{9,-8}},5); Path({{9,-16},{19,-16}},4);
            Line({{X-7*Size,Y+10*Size},{X+7*Size,Y+10*Size}},Ink,3*Size);
            Line({{X,Y+3*Size},{X,Y+17*Size}},Ink,3*Size);
        }
    }
    void Face(float X,float Y,float Size,bool Dead,bool Sad,bool Happy,float Time,FLinearColor PlayerTint) const {
        const float Bob=Dead?0:Happy?FMath::Sin(Time*9)*2:FMath::Sin(Time*2)*.65f;
        Y+=Bob;
        // A two-crown silhouette with separate roots, kept readable at portrait size.
        const FLinearColor BodyTint=Dead?PlayerTint.Desaturate(.8f)*.6f:PlayerTint;
        Box(X-22*Size,Y-22*Size,44*Size,41*Size,BodyTint,13*Size);
        Box(X-19*Size,Y+3*Size,15*Size,32*Size,BodyTint,7*Size);
        Box(X+4*Size,Y+3*Size,15*Size,32*Size,BodyTint,7*Size);
        const FLinearColor Eye(.028f,.030f,.043f);
        for(float DX:{-9.f,9.f}) {
            if(Dead) { Line({{X+(DX-4)*Size,Y-6*Size},{X+(DX+4)*Size,Y+2*Size}},Eye,3*Size); Line({{X+(DX+4)*Size,Y-6*Size},{X+(DX-4)*Size,Y+2*Size}},Eye,3*Size); }
            else if(Happy) Line({{X+(DX-4)*Size,Y-2*Size},{X+DX*Size,Y-6*Size},{X+(DX+4)*Size,Y-2*Size}},Eye,2.5f*Size);
            else Box(X+(DX-2)*Size,Y-5*Size,4*Size,5*Size,Eye,2*Size);
        }
        Line({{X-6*Size,Y+(Sad?12:6)*Size},{X,Y+(Sad?8:Happy?14:10)*Size},{X+6*Size,Y+(Sad?12:6)*Size}},Eye,2.4f*Size);
    }
};
}
int32 UMCHUDIcon::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    const float S=FMath::Min(Geometry.GetLocalSize().X/80,Geometry.GetLocalSize().Y/80);
    MCGameplayHUDPrivate::FHUDPainter P{Geometry,Elements,Layer,S};
    if(bTooth) {
        P.Face(40,34,.95f,bDead,bSad,bHappy,GetWorld()?GetWorld()->GetTimeSeconds():0,Tint);
        if(bHost) P.Line({{26,9},{23,1},{32,6},{40,0},{48,6},{57,1},{54,9},{26,9}},MCGameplayHUDPrivate::Amber,3);
    }
    else P.Tool(40,40,ToolSlot,Tint,1.15f);
    return Layer+3;
}
void UMCGameplayHUD::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if(GetOwningPlayer() && !StaminaWidget)
    {
        auto* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
        if(!Root)
        {
            UWidget* Existing=WidgetTree->RootWidget;
            Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
            if(Existing) {auto* Full=Root->AddChildToCanvas(Existing);Full->SetAnchors(FAnchors(0,0,1,1));Full->SetOffsets(FMargin(0));}
        }
        StaminaWidget=CreateWidget<UMCStaminaWidget>(GetOwningPlayer(),StaminaWidgetClass?StaminaWidgetClass.Get():UMCStaminaWidget::StaticClass());
        auto* StaminaSlot=Root->AddChildToCanvas(StaminaWidget);StaminaSlot->SetAnchors(FAnchors(.5f,1));StaminaSlot->SetAlignment(FVector2D(.5f,1));
        StaminaSlot->SetPosition(FVector2D(0,-138));StaminaSlot->SetSize(FVector2D(250,58));
    }
    EnsureDirectorMonitor();
    EnsureProgressionPanel();
}
void UMCGameplayHUD::EnsureProgressionPanel()
{
    if(ProgressionPanel || !WidgetTree) return;
    auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot")));
    if(!Root) Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(!Root) return;
    ProgressionPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TeamProgression"));
    ProgressionPanel->SetPadding(FMargin(14,8)); ProgressionPanel->SetBrushColor(MCGameplayHUDPrivate::Ink);
    ProgressionPanel->SetVisibility(ESlateVisibility::Collapsed);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(); ProgressionPanel->SetContent(Column);
    ProgressionText=WidgetTree->ConstructWidget<UTextBlock>();
    ProgressionText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14));
    ProgressionText->SetColorAndOpacity(FSlateColor(MCGameplayHUDPrivate::White)); Column->AddChildToVerticalBox(ProgressionText);
    ProgressionBar=WidgetTree->ConstructWidget<UProgressBar>(); ProgressionBar->SetFillColorAndOpacity(MCGameplayHUDPrivate::Mint);
    Column->AddChildToVerticalBox(ProgressionBar);
    auto* XPPanelSlot=Root->AddChildToCanvas(ProgressionPanel); XPPanelSlot->SetAnchors(FAnchors(.5f,0));
    XPPanelSlot->SetAlignment(FVector2D(.5f,0)); XPPanelSlot->SetPosition(FVector2D(0,116)); XPPanelSlot->SetSize(FVector2D(330,58));
}
void UMCGameplayHUD::EnsureDirectorMonitor()
{
    if((DirectorPanel && DirectorCandidatesPanel && DirectorLogButton) || !WidgetTree) return;
    // Use the authored design canvas so the sidebar follows its viewport scale.
    auto* Root=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("HUDRoot")));
    if(!Root) Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(!Root) return;
    auto AddPanel=[&](FName Name,TObjectPtr<UBorder>& Panel,TObjectPtr<UTextBlock>& TextWidget,float Width,float RightOffset,float Top)
    {
        Panel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),Name);
        Panel->SetPadding(FMargin(12,10));Panel->SetBrushColor(MCGameplayHUDPrivate::Ink);
        Panel->SetVisibility(ESlateVisibility::Collapsed);
        auto* PanelSize=WidgetTree->ConstructWidget<USizeBox>();
        PanelSize->SetWidthOverride(Width);PanelSize->SetMaxDesiredHeight(600);
        PanelSize->SetClipping(EWidgetClipping::ClipToBounds);Panel->SetContent(PanelSize);
        TextWidget=WidgetTree->ConstructWidget<UTextBlock>();
        TextWidget->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11));
        TextWidget->SetColorAndOpacity(FSlateColor(MCGameplayHUDPrivate::White));
        TextWidget->SetWrapTextAt(Width);TextWidget->SetAutoWrapText(false);
        PanelSize->SetContent(TextWidget);
        auto* PanelSlot=Root->AddChildToCanvas(Panel);PanelSlot->SetAnchors(FAnchors(1,0));
        PanelSlot->SetAlignment(FVector2D(1,0));PanelSlot->SetPosition(FVector2D(RightOffset,Top));PanelSlot->SetAutoSize(true);
        return PanelSize;
    };
    if(!DirectorPanel) DirectorPanelSize=AddPanel(TEXT("DirectorMonitor"),DirectorPanel,DirectorText,280,-22,338);
    // The second column ends before the observation column and stays right of
    // the arena centre, leaving the player portraits and bottom controls clear.
    if(!DirectorCandidatesPanel) AddPanel(TEXT("DirectorCandidates"),DirectorCandidatesPanel,DirectorCandidatesText,306,-362,144);
    if (!DirectorLogButton) {
        DirectorLogButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("DirectorLogButton"));
        DirectorLogButton->SetBackgroundColor(MCGameplayHUDPrivate::Ink);
        DirectorLogButton->SetToolTipText(FText::FromString(TEXT("Открыть общий журнал забега и решения директора. F3 доступен также с клавиатуры.")));
        DirectorLogButtonText=WidgetTree->ConstructWidget<UTextBlock>();
        DirectorLogButtonText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11));
        DirectorLogButtonText->SetColorAndOpacity(FSlateColor(MCGameplayHUDPrivate::Mint));
        DirectorLogButtonText->SetText(FText::FromString(TEXT("F3 · ЖУРНАЛ ДИРЕКТОРА")));
        auto* Content=CastChecked<UButtonSlot>(DirectorLogButton->AddChild(DirectorLogButtonText));
        Content->SetPadding(FMargin(12,8));
        DirectorLogButton->OnClicked.AddDynamic(this,&UMCGameplayHUD::OpenDirectorLog);
        auto* LogSlot=Root->AddChildToCanvas(DirectorLogButton);LogSlot->SetAnchors(FAnchors(1,0));
        LogSlot->SetAlignment(FVector2D(1,0));LogSlot->SetPosition(FVector2D(-22,96));LogSlot->SetAutoSize(true);
        LogSlot->SetZOrder(10);
    }
}
void UMCGameplayHUD::OpenDirectorLog()
{
    if (auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->ToggleDevPanel();
}
void UMCGameplayHUD::RefreshDirectorMonitor(const FMCGameDirectorState& State)
{
    EnsureDirectorMonitor();
    if(!DirectorPanel || !DirectorText) return;
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr;
    const bool Debug=(State.bEnabled || (GS && !GS->DirectorDecisionLog.IsEmpty())) && MCGameplayHUDPrivate::DirectorDebug.GetValueOnGameThread()>0;
    if (DirectorLogButton) {
#if UE_BUILD_SHIPPING
        DirectorLogButton->SetVisibility(ESlateVisibility::Collapsed);
#else
        DirectorLogButton->SetVisibility(ESlateVisibility::Visible);
#endif
        const bool FragmentComplete=GS && GS->SingleDayDirector && GS->SingleDayDirector->bAuthoredFragmentComplete;
        DirectorLogButtonText->SetText(FText::FromString(FragmentComplete?TEXT("ФРАГМЕНТ ГОТОВ · F3 / ЖУРНАЛ"):TEXT("F3 · ЖУРНАЛ ДИРЕКТОРА")));
    }
    DirectorPanel->SetVisibility(State.bEnabled || Debug?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(DirectorCandidatesPanel) DirectorCandidatesPanel->SetVisibility(Debug?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* Events=Find(TEXT("EventsPanel")))
        Events->SetVisibility(Debug?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(!State.bEnabled) return;
    const float Width=Debug?306.f:280.f;
    DirectorPanelSize->SetWidthOverride(Width);DirectorText->SetWrapTextAt(Width);
    if(auto* PanelSlot=Cast<UCanvasPanelSlot>(DirectorPanel->Slot)) PanelSlot->SetPosition(FVector2D(-22,Debug?144:338));
    if(GS && GS->bSingleDayLoop && !Debug) {
        if(State.Instruction.IsEmpty()) DirectorPanel->SetVisibility(ESlateVisibility::Collapsed);
        DirectorText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14));
        DirectorText->SetText(FText::FromString(State.Instruction));
        return;
    }
    DirectorText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11));
    using namespace MCGameplayHUDPrivate;
    FString Value=PacingName(State.Pacing)+FString::Printf(TEXT(" · сложность %.2f\nНагрузка %.2f · цель %.2f"),State.Difficulty,State.Pressure,State.TargetPressure);
    if(!State.Instruction.IsEmpty()) Value+=TEXT("\n")+ShortLine(State.Instruction,92);
    Value+=(State.bNextReserved?TEXT("\nОкно зарезервировано: "):TEXT("\nКандидат: "))
        +(State.NextTitle.IsEmpty()?TEXT("ожидание"):ShortLine(State.NextTitle,70));
    if(!State.DecisionReason.IsEmpty()) Value+=TEXT("\n")+ShortLine(State.DecisionReason,120);
    if(Debug)
    {
        Value+=FString::Printf(TEXT("\n\nПредел %.2f · работа ≈ %.0f чел·с\nТемп %.2f чел·с/с/доступного\nЗдоровье %d%% · запас сил %d%%\nСтресс %d%% · игроков %d · доступны %d\nДень прошёл на %d%%\nИсходная еда %d/%d · готово %d%%\nзавершено / фактически подано\nЦелых %d · кусочков %d · несут %d\nГлотка: %d в очереди · движение %s\nУборка %d · лёд %d · огонь %d · язвы %d\nОчередь событий %d · срочно: %s"),
            State.PressureLimit,State.WorkSeconds,State.Throughput,Percent(State.TeamHealth),Percent(State.TeamStamina),Percent(State.Stress),
            State.LivingPlayers,State.AvailablePlayers,Percent(State.DayProgress),State.FinishedFood,State.SpawnedFood,Percent(State.CompletionProgress),
            State.WholeFood,State.Fragments,State.CarriedFood,State.ThroatQueued,State.bGlobalMovement?TEXT("да"):TEXT("нет"),
            State.CleaningTasks,State.Ice,State.Fires,State.Ulcers,State.QueuedEvents,State.bUrgent?TEXT("да"):TEXT("нет"));
        if(!State.LastDecision.IsEmpty()) Value+=TEXT("\nРешение: ")+ShortLine(State.LastDecision,100);
        Value+=TEXT("\nMC.Director.Debug 0 — свернуть\n\nПОСЛЕДНИЕ РЕШЕНИЯ · НОВЫЕ СВЕРХУ");
        // Keep the newest decisions visible within the observation panel's height.
        const auto& History=GS?GS->DirectorDecisionLog:State.DecisionLog;
        for(int32 I=History.Num()-1;I>=FMath::Max(0,History.Num()-4);--I)
            Value+=TEXT("\n")+ShortLine(History[I],40);

        TArray<const FMCGameDirectorCandidate*,TInlineAllocator<16>> Candidates;
        for(const auto& Candidate:State.Candidates) Candidates.Add(&Candidate);
        Candidates.Sort([](const FMCGameDirectorCandidate& A,const FMCGameDirectorCandidate& B)
        {
            const float AP=CandidateProbability(A),BP=CandidateProbability(B);
            return AP==BP?uint8(A.Kind)<uint8(B.Kind):AP>BP;
        });
        FString Weights=TEXT("КАНДИДАТЫ · ВЕРОЯТНОСТЬ ВЫБОРА\nВес базовый → итоговый · шанс\nP — прогноз нагрузки после события");
        for(const auto* Candidate:Candidates)
        {
            Weights+=FString::Printf(TEXT("\n%s  %.1f→%.1f · %.1f%%\nP %.2f · %s"),*CandidateName(Candidate->Kind),
                Candidate->BaseWeight,Candidate->EffectiveWeight,CandidateProbability(*Candidate)*100,Candidate->ForecastPressure,
                *ShortLine(Candidate->BlockReason.IsEmpty()?TEXT("доступно"):Candidate->BlockReason,60));
        }
        Weights+=FString::Printf(TEXT("\n\nЖдать: вес %.1f · %.1f%%\nВыбор включает ожидание."),State.WaitWeight,
            State.WaitWeight>0?FMath::Clamp(State.WaitProbability,0.f,1.f)*100:0.f);
        if(DirectorCandidatesText && !DirectorCandidatesText->GetText().ToString().Equals(Weights))
            DirectorCandidatesText->SetText(FText::FromString(Weights));
    }
    else Value+=TEXT("\nMC.Director.Debug 1 — наблюдения и веса");
    if(!DirectorText->GetText().ToString().Equals(Value)) DirectorText->SetText(FText::FromString(Value));
}
void UMCGameplayHUD::NativeConstruct()
{
    Super::NativeConstruct();
    Widgets.Reset();
    TArray<UWidget*> All; WidgetTree->GetAllWidgets(All);
    for(auto* Widget:All) {
        Widgets.Add(Widget->GetFName(),Widget);
        // Tooth portraits bob in paint; cache their surrounding layout without freezing the animation.
        if(auto* Icon=Cast<UMCHUDIcon>(Widget);Icon && Icon->bTooth) Icon->ForceVolatile(true);
    }
    RefreshState();
}
UWidget* UMCGameplayHUD::Find(FName Name) const
{ const auto* Widget=Widgets.Find(Name); return Widget?Widget->Get():nullptr; }
void UMCGameplayHUD::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt); RefreshElapsed+=Dt;
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const bool Spray=Hero && Hero->Inventory && Hero->Inventory->Selected==EMCToolSlot::Spray;
    // Charge and cooldown animate every frame; ordinary HUD data remains at 10 Hz.
    if(Spray || bHadSprayReticle) if(const auto Cached=GetCachedWidget()) Cached->Invalidate(EInvalidateWidgetReason::Paint);
    bHadSprayReticle=Spray;
    if(RefreshElapsed>=.1f) { RefreshElapsed=0; RefreshState(); }
}
int32 UMCGameplayHUD::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    const int32 Top=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
    const auto* PC=Cast<AMCPlayerController>(GetOwningPlayer());
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    const auto* Inv=Hero?Hero->Inventory.Get():nullptr;
    if(!PC || !PC->IsLocalController() || PC->bShowMouseCursor || PC->IsLookInputIgnored() || PC->IsSpectating()
        || !Inv || Inv->Selected!=EMCToolSlot::Spray || !Hero->CanWork() || Hero->bInCoffee || !Inv->ShouldPresentTool()) return Top;
    const auto PlayerGeometry=UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(GetOwningPlayer());
    const FVector2D Center=Geometry.AbsoluteToLocal(PlayerGeometry.LocalToAbsolute(PlayerGeometry.GetLocalSize()*.5f));
    const bool Pressure=Inv->SelectedUpgrade()==EMCToolUpgrade::Watergun && Inv->bPressureMode;
    const float Charge=Inv->WaterChargeFraction(),Cool=Pressure?Inv->SpraySecondsLeft():0;
    const FLinearColor Tint=Cool>0?FLinearColor(.48f,.58f,.65f):Pressure?FLinearColor(.86f,.97f,1):FLinearColor(.20f,.80f,1);
    auto Stroke=[&](const TArray<FVector2D>& Points,FLinearColor Color,float Width=2.f) {
        FSlateDrawElement::MakeLines(Elements,Top+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.005f,.015f,.025f,.8f),true,Width+2);
        FSlateDrawElement::MakeLines(Elements,Top+2,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);
    };
    auto Arc=[&](float Radius,float Fraction,FLinearColor Color) {
        if(Fraction<=0) return;
        TArray<FVector2D> Points;
        const int32 Steps=FMath::Max(2,FMath::CeilToInt(40*Fraction));
        for(int32 I=0;I<=Steps;++I) {
            const float A=-PI/2+2*PI*Fraction*I/Steps;
            Points.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);
        }
        Stroke(Points,Color);
    };
    const float Radius=Pressure?7.f:10.f;
    Arc(Radius,1,Tint);
    for(const FVector2D Axis:{FVector2D(1,0),FVector2D(-1,0),FVector2D(0,1),FVector2D(0,-1)})
        Stroke({Center+Axis*(Radius+4),Center+Axis*(Radius+9)},Tint);
    const FSlateRoundedBoxBrush Dot(FLinearColor::White,2.f);
    FSlateDrawElement::MakeBox(Elements,Top+2,Geometry.ToPaintGeometry(FVector2D(4,4),FSlateLayoutTransform(Center-FVector2D(2,2))),&Dot,ESlateDrawEffect::None,Tint);
    if(Pressure) {
        Arc(18,1,FLinearColor(.22f,.32f,.40f,.65f));
        Arc(18,Cool>0?1-Cool/4.f:Charge,FLinearColor(.20f,.85f,1));
    }
    return Top+3;
}
void UMCGameplayHUD::RefreshState()
{
    namespace HUD = MCGameplayHUDPrivate;
    const auto* GS=GetWorld()?GetWorld()->GetGameState<AMCGameState>():nullptr; if(!GS) return;
    const auto* Hero=Cast<AMCToothCharacter>(GetOwningPlayerPawn());
    if(StaminaWidget) StaminaWidget->Refresh();
    auto Text=[&](FName Name,const FString& Value) { if(auto* T=Cast<UTextBlock>(Find(Name));T && !T->GetText().ToString().Equals(Value)) T->SetText(FText::FromString(Value)); };
    auto Bar=[&](FName Name,float Value) { if(auto* B=Cast<UProgressBar>(Find(Name))) B->SetPercent(FMath::Clamp(Value,0.f,1.f)); };
    auto Show=[&](FName Name,bool Visible) { if(auto* W=Find(Name)) W->SetVisibility(Visible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed); };
    auto Color=[&](FName Name,FLinearColor Value) { if(auto* I=Cast<UImage>(Find(Name))) I->SetColorAndOpacity(Value); };
    const double Now=GS->GetServerWorldTimeSeconds();
    EnsureProgressionPanel();
    if(ProgressionPanel) {
        ProgressionPanel->SetVisibility(GS->bSingleDayLoop && !GS->bTutorialActive?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(GS->Progression) {
            const auto* XP=GS->Progression.Get();
            ProgressionText->SetText(FText::FromString(XP->ExperienceToNextLevel>0
                ? FString::Printf(TEXT("КОМАНДА · УР. %d     %lld / %d XP"),XP->TeamLevel,XP->ExperienceInLevel,XP->ExperienceToNextLevel)
                : FString::Printf(TEXT("КОМАНДА · УР. %d     МАКСИМУМ"),XP->TeamLevel)));
            ProgressionBar->SetPercent(XP->ExperienceToNextLevel>0?FMath::Clamp(float(XP->ExperienceInLevel)/XP->ExperienceToNextLevel,0.f,1.f):1.f);
        }
    }
    const auto& Director=GS->DirectorState;const bool Directed=Director.bEnabled;
    RefreshDirectorMonitor(Director);
    const bool HasStep=!Directed && GS->DayPlan && GS->DayPlan->Steps.IsValidIndex(GS->StepIndex);
    const bool Finished=(!Directed && GS->bDayOneComplete) || GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost;
    FString Title=Directed?(Director.CurrentTitle.IsEmpty()?TEXT("СКОРО НАЧНЁМ"):Director.CurrentTitle):HasStep?HUD::StepName(GS->DayPlan->Steps[GS->StepIndex].Step):GS->CurrentEvent?GS->CurrentEvent->Title.ToString():TEXT("СКОРО НАЧНЁМ");
    const AMCCoffeeFlood* ActiveFlood=nullptr;
    for(TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It) if(It->IsActive()) { ActiveFlood=*It; break; }
    if(!Directed && ActiveFlood) Title=ActiveFlood->bRiverFlood?TEXT("ЦУНАМИ"):ActiveFlood->GetPhase()==EMCCoffeePhase::Holding?TEXT("КОФЕ · ПЛАВАНИЕ"):TEXT("old_flood");
    if(Directed && GS->Phase==EMCShiftPhase::Intermission) Title=TEXT("ПЕРЕДЫШКА");
    if(Finished) Title=GS->Phase==EMCShiftPhase::Lost?TEXT("РОТ НЕ СПАСЁН"):TEXT("ДЕНЬ ЗАВЕРШЁН");
    Text(TEXT("DayTitle"),FString::Printf(TEXT("ДЕНЬ %d"),FMath::Max(1,GS->Day))); Text(TEXT("EventTitle"),Title);
    const float Done=Directed?FMath::Clamp(Director.CompletionProgress,0.f,1.f):GS->TasksTotal>0?1-float(GS->TasksLeft)/GS->TasksTotal:0;
    int32 Surfaces=0; float Clean=0;
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        if(const auto* Patch=Cast<AMCMouthSurface>(*It);Patch && Patch->bUlcer) continue;
        if(const auto* Tooth=Cast<AMCArenaTooth>(*It);Tooth && !Tooth->IsAvailable()) continue;
        if(const auto* Care=It->FindComponentByClass<UMCToothStatusComponent>()) { ++Surfaces; Clean+=1-Care->CoffeeAmount(); }
    }
    Clean=Surfaces?Clean/Surfaces:1;
    const float Health=GS->MouthHealth/FMath::Max(1.f,GS->RunSettings.MaxMouthHealth);
    Text(TEXT("TaskLabel"),Directed && !GS->bSingleDayLoop?TEXT("ЕДА ЗАВЕРШЕНА"):TEXT("ТЕКУЩАЯ ЗАДАЧА"));
    Text(TEXT("TaskValue"),Directed?FString::Printf(TEXT("%d/%d"),Director.FinishedFood,Director.SpawnedFood)
        :FString::Printf(TEXT("%d/%d"),FMath::Max(0,GS->TasksTotal-GS->TasksLeft),GS->TasksTotal)); Bar(TEXT("TaskProgress"),Done);
    Text(TEXT("CleanValue"),FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Clean*100))); Bar(TEXT("CleanProgress"),Clean);
    Text(TEXT("HealthValue"),FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Health*100))); Bar(TEXT("HealthProgress"),Health);
    const double EndAt=Directed && GS->Phase==EMCShiftPhase::Working?Director.DayEndAt:GS->PhaseEndsAt;
    const bool Timed=EndAt>0 && !Finished && (Directed || !GS->bDevManualEvents);
    const float Remaining=float(FMath::Max(0.,EndAt-Now));const int32 Seconds=FMath::CeilToInt(Remaining);
    const float Duration=FMath::Max(1.f,float(EndAt-GS->StepStartedAt));
    const float Progress=Directed && GS->Phase==EMCShiftPhase::Working?FMath::Clamp(Director.DayProgress,0.f,1.f)
        :Timed?FMath::Clamp(1-Remaining/Duration,0.f,1.f):Done;
    Text(TEXT("TimerValue"),Timed?FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60):TEXT("--:--"));
    Text(TEXT("TimerLabel"),Finished?TEXT("ИТОГИ ДНЯ"):GS->bSingleDayLoop?Timed?TEXT("ДО СЛЕД. ЭВЕНТА"):TEXT("ПРОДЕРЖИСЬ И ПРОКАЧАЙСЯ"):Timed?Directed?GS->Phase==EMCShiftPhase::Working?TEXT("ДО КОНЦА ДНЯ"):TEXT("ДО НАЧАЛА ДНЯ"):TEXT("ДО СЛЕД. СОБЫТИЯ"):TEXT("БЕЗ ЛИМИТА ВРЕМЕНИ")); Bar(TEXT("TimerProgress"),Finished?1:Progress);
    if(auto* T=Cast<UTextBlock>(Find(TEXT("TimerValue")))) T->SetColorAndOpacity(FSlateColor(Timed && Seconds<10?HUD::Amber:HUD::White));
    TArray<APlayerState*> Players;
    for(const auto& Player:GS->PlayerArray)
        if(IsValid(Player) && !Player->IsSpectator() && !Player->IsOnlyASpectator()) Players.Add(Player.Get());
    Players.Sort([](const APlayerState& A,const APlayerState& B){return A.GetPlayerId()<B.GetPlayerId();});
    if(auto* Strip=Find(TEXT("PlayersPanel"))) Strip->SetRenderTranslation(FVector2D(76*(4-FMath::Min(4,Players.Num())),0));
    for(int32 I=0;I<4;++I) {
        const FString N=FString::Printf(TEXT("Player%d"),I+1); Show(FName(N),Players.IsValidIndex(I)); if(!Players.IsValidIndex(I)) continue;
        const auto* Tooth=Cast<AMCToothCharacter>(Players[I]->GetPawn()); const bool Dead=Tooth && !Tooth->Status->IsAlive();
        const auto* State=Cast<AMCPlayerState>(Players[I]);
        const FLinearColor PlayerTint=State?State->PlayerColor:Tooth?Tooth->GetPlayerColor():HUD::White;
        Color(FName(N+TEXT("Frame")),Dead?PlayerTint.Desaturate(.8f)*.6f:PlayerTint);
        if(auto* Icon=Cast<UMCHUDIcon>(Find(FName(N+TEXT("Face"))))) {
            const bool Host=State && State->bSessionHost;
            const bool Sad=Tooth && (Tooth->MimicCaptor || Now-Tooth->TaskFailureAt<2.5 || Tooth->Status->State.Health<Tooth->Status->State.MaxHealth*.35f);
            const bool Happy=Tooth && Now-Tooth->TaskSuccessAt<2 && !Sad;
            if(!Icon->Tint.Equals(PlayerTint) || Icon->bHost!=Host || Icon->bDead!=Dead || Icon->bSad!=Sad || Icon->bHappy!=Happy) {
                Icon->Tint=PlayerTint;Icon->bHost=Host;Icon->bDead=Dead;Icon->bSad=Sad;Icon->bHappy=Happy;
                if(const auto Cached=Icon->GetCachedWidget()) Cached->Invalidate(EInvalidateWidgetReason::Paint);
            }
        }
        Bar(FName(N+TEXT("Health")),Tooth?Tooth->Status->State.Health/FMath::Max(1.f,Tooth->Status->State.MaxHealth):0);
    }
    TArray<int32,TInlineAllocator<16>> TimelineSteps;
    if(HasStep) for(int32 I=0;I<GS->DayPlan->Steps.Num();++I)
        if(GS->DayPlan->Steps[I].Step!=EMCDayStep::DiscardBrushes) TimelineSteps.Add(I);
    const int32 TimelineStart=FMath::Max(0,TimelineSteps.IndexOfByKey(GS->StepIndex)-1);
    for(int32 I=0;I<3;++I) {
        const FString N=FString::Printf(TEXT("Timeline%d"),I);
        if(Directed)
        {
            const bool Visible=I==0 || I==1 && !Director.NextTitle.IsEmpty();Show(FName(N),Visible);
            if(!Visible) continue;
            Text(FName(N+TEXT("Label")),I==0?TEXT("СЕЙЧАС"):GS->bSingleDayLoop?TEXT("ДАЛЕЕ"):Director.bNextReserved?TEXT("ЗАРЕЗЕРВИРОВАНО"):TEXT("КАНДИДАТ"));
            Text(FName(N+TEXT("Title")),I==0?Title:Director.NextTitle);
            Bar(FName(N+TEXT("Fill")),I==0?Finished?1:Progress:0);
            if(auto* B=Cast<UProgressBar>(Find(FName(N+TEXT("Fill"))))) B->SetFillColorAndOpacity(FLinearColor(.04f,.24f,.24f,.66f));
            Color(FName(N+TEXT("Accent")),I==0?HUD::Mint:HUD::Muted);
            continue;
        }
        const bool Visible=HasStep?TimelineSteps.IsValidIndex(TimelineStart+I):I==0; Show(FName(N),Visible); if(!Visible) continue;
        const int32 Index=HasStep?TimelineSteps[TimelineStart+I]:INDEX_NONE;
        const bool Past=HasStep && Index<GS->StepIndex,Current=!HasStep || Index==GS->StepIndex,Failed=Past && GS->PreviousStepFailed;
        Text(FName(N+TEXT("Label")),Past?Failed?TEXT("ПРОШЛО · НЕ ПОЛНОСТЬЮ"):TEXT("ВЫПОЛНЕНО"):Current?TEXT("СЕЙЧАС"):TEXT("ДАЛЕЕ"));
        Text(FName(N+TEXT("Title")),Current?Title:HasStep?HUD::StepName(GS->DayPlan->Steps[Index].Step):Title);
        Bar(FName(N+TEXT("Fill")),Past?1:Current?Finished?1:Progress:0);
        if(auto* B=Cast<UProgressBar>(Find(FName(N+TEXT("Fill"))))) B->SetFillColorAndOpacity(Failed?FLinearColor(.36f,.14f,.035f,.65f):FLinearColor(.04f,.24f,.24f,.66f));
        Color(FName(N+TEXT("Accent")),Current?HUD::Mint:Past?Failed?HUD::Amber:HUD::Muted:FLinearColor(.12f,.17f,.22f));
    }
    Show(TEXT("InventoryPanel"),Hero!=nullptr); Show(TEXT("HintPanel"),Hero!=nullptr);
    if(Hero && Hero->Inventory) {
        const auto* Inv=Hero->Inventory.Get(); Text(TEXT("ToolTitle"),Inv->ToolName());
        for(int32 I=0;I<4;++I) {
            const FString N=FString::Printf(TEXT("Tool%d"),I+1); const bool Selected=uint8(Inv->Selected)==I;
            Color(FName(N+TEXT("Frame")),Selected?HUD::Mint:FLinearColor(.15f,.23f,.29f,.8f));
            Color(FName(N+TEXT("KeyBG")),Selected?HUD::Mint:HUD::White);
        }
        const float Cool=Inv->SpraySecondsLeft(); Show(TEXT("SprayCooldown"),Cool>0 || Inv->bChargingWater);
        Text(TEXT("CooldownValue"),Inv->bChargingWater?FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Inv->WaterChargeFraction()*100)):FString::Printf(TEXT("%.1f"),Cool));
        Bar(TEXT("CooldownProgress"),Inv->bChargingWater?Inv->WaterChargeFraction():1-Cool/Inv->CooldownSeconds());
        FString Hint=Inv->Selected==EMCToolSlot::Pickaxe?TEXT("ЛКМ · ДРОБИТЬ · ЕДА → XP"):Inv->Selected==EMCToolSlot::Knife?TEXT("ЛКМ · РАЗБИТЬ ЕДУ → XP"):Inv->Selected==EMCToolSlot::Spray?TEXT("УДЕРЖИВАЙ ЛКМ · ЛЕЧИТЬ ЯЗВУ"):TEXT("ЛКМ · ЧИСТИТЬ");
        if(Inv->Selected==EMCToolSlot::Knife && Inv->HasUpgrade(EMCToolUpgrade::Chainsaw)) Hint=TEXT("УДЕРЖИВАЙ ЛКМ · ПИЛИТЬ И ДВИГАТЬСЯ ВПЕРЁД");
        if(Inv->Selected==EMCToolSlot::Spray && Inv->HasUpgrade(EMCToolUpgrade::Watergun)) Hint=Inv->bPressureMode?TEXT("ЗАЖМИ ЛКМ · ЗАРЯДИТЬ, ОТПУСТИ · ВЫСТРЕЛ     4 · РЕЖИМ"):TEXT("УДЕРЖИВАЙ ЛКМ · ТУШИТЬ И ЛЕЧИТЬ     4 · РЕЖИМ");
        Hint+=TEXT("     ПКМ · ДЕРЖАТЬСЯ");
        const auto IsFreshOrdinaryFood=[](const AMCFoodActor* Food) {
            return IsValid(Food) && Food->FoodData.Kind==EMCFoodKind::Food && !Food->bBrushTool && !Food->IsWrongIngredient();
        };
        bool HasOrdinaryPieces=false,HasExitPieces=false;
        for(const auto& Piece:Hero->FoodCollection->Pieces) if(IsValid(Piece)) {
            HasOrdinaryPieces|=IsFreshOrdinaryFood(Piece);HasExitPieces|=!IsFreshOrdinaryFood(Piece);
        }
        const bool HeldNeedsExit=IsValid(Hero->HeldFood) && !IsFreshOrdinaryFood(Hero->HeldFood);
        const bool ExplicitFoodTransport=GS->bSingleDayLoop
            ?!HeldNeedsExit && !HasExitPieces && (IsFreshOrdinaryFood(Hero->HeldFood) || Hero->FoodCollection->bCollecting || HasOrdinaryPieces)
            :IsValid(Hero->HeldFood) || Hero->FoodCollection->bCollecting || !Hero->FoodCollection->Pieces.IsEmpty();
        if(Hero->FoodCollection->bCollecting)
        {
            bool HasWrong=GS->bSingleDayLoop && HasExitPieces;
            for(const auto& Piece:Hero->FoodCollection->Pieces) if(IsValid(Piece) && Piece->IsWrongIngredient()) { HasWrong=true; break; }
            Hint=HasWrong?FString::Printf(TEXT("СТОПКА %d/6 · В КРАСНУЮ · Q БРОСИТЬ"),Hero->FoodCollection->Pieces.Num())
                :FString::Printf(TEXT("СТОПКА %d/6 · В ЗЕЛЁНУЮ · Q БРОСИТЬ"),Hero->FoodCollection->Pieces.Num());
        }
        if(Hero->IsYawning()) Hint=TEXT("ЗЕВАНИЕ · WASD + SHIFT · БЕГИ ПРОТИВ ПОТОКА");
        if(Hero->HeldFood) Hint=GS->bSingleDayLoop && Hero->HeldFood->FoodData.Kind==EMCFoodKind::Spicy?TEXT("ПЕРЕЦ — В КРАСНУЮ / ВЫХОД · Q БРОСИТЬ")
            :Hero->HeldFood->IsWrongIngredient()?TEXT("МУСОР — В КРАСНУЮ · Q БРОСИТЬ"):TEXT("E · ДЕРЖАТЬ     Q · БРОСИТЬ");
        if(Hero->bInCoffee) Hint=ActiveFlood && ActiveFlood->bRiverFlood?
            TEXT("WASD + SHIFT · БЕЖАТЬ     ПКМ · ДЕРЖАТЬСЯ ЗА ОПОРУ"):
            TEXT("WASD · ПЛЫТЬ     ЛКМ · ЗАЦЕПИТЬСЯ");
        if(const auto* Move=Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()); Move && Move->IsClimbing()) Hint=TEXT("WASD · ЛАЗАТЬ     E · ДЕРЖАТЬСЯ     SPACE · ОТПРЫГНУТЬ");
        for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {
            if(It->CanOrderJump(Hero) && (!GS->bSingleDayLoop || ExplicitFoodTransport)) Hint=TEXT("SPACE · ПРЫГНУТЬ НА ЯЗЫЧОК");
            if(It->ContainsPlayer(Hero)) {
                if(It->ThroatPhase==EMCThroatPhase::Anticipation) {
                    const double UntilSuction=FMath::Max(0.,It->PhaseStartedAt+It->AnticipationSeconds-Now);
                    Hint=GS->bSingleDayLoop && !ExplicitFoodTransport
                        ?FString::Printf(TEXT("ЗАСАСЫВАНИЕ ЧЕРЕЗ %.1f С · ВЫЙДИ ИЗ ЗОНЫ"),UntilSuction)
                        :FString::Printf(TEXT("ДОСТАВЛЯЙ ЕЩЁ · ЗАСАСЫВАНИЕ ЧЕРЕЗ %.1f С"),UntilSuction);
                }
                else if(It->ThroatPhase==EMCThroatPhase::Swallowing) Hint=GS->bSingleDayLoop && !ExplicitFoodTransport?TEXT("ГЛОТКА ЗАСАСЫВАЕТ · ДЕРЖИСЬ"):TEXT("ГЛОТКА ЗАСАСЫВАЕТ · СЛЕДУЮЩАЯ ПАРТИЯ ПРИНИМАЕТСЯ");
                else if(It->ThroatPhase==EMCThroatPhase::Collecting && (!GS->bSingleDayLoop || ExplicitFoodTransport)) Hint=TEXT("ВНЕСИ СТОПКУ В ЗОНУ · ЕДА ОТПРАВИТСЯ САМА");
            }
        }
        if(Hero->Grip && Hero->Grip->IsBracing())
            Hint=FString::Printf(TEXT("ДЕРЖИ ПКМ · %s · НАГРУЗКА %.0f КГ"),
                Cast<AMCToothCharacter>(Hero->Grip->BraceTarget())?TEXT("ТЯНЕШЬ ИГРОКА"):Hero->Grip->IsWorldAnchored()?TEXT("ОПОРА"):TEXT("ДЕРЖИШЬСЯ ЗА ЕДУ"),Hero->Grip->TotalChainMass());
        else if(Hero->Grip && Hero->Grip->IncomingChainMass()>0)
            Hint=FString::Printf(TEXT("ТЕБЯ ДЕРЖАТ · ДОПОЛНИТЕЛЬНЫЙ ВЕС %.0f КГ"),Hero->Grip->IncomingChainMass());
        if(const auto* PC=Cast<AMCPlayerController>(GetOwningPlayer());PC && PC->IsSpectating())
        {
            const auto* Target=PC->GetSpectatorTarget();
            const auto* Player=Target?Target->GetPlayerState():nullptr;
            const FString Name=Player?Player->GetPlayerName():TEXT("ожидание игрока");
            const float Respawn=PC->GetRespawnSecondsRemaining();
            Hint=FString::Printf(TEXT("НАБЛЮДАЕШЬ: %s · ЛКМ / → СЛЕДУЮЩИЙ · ПКМ / ← ПРЕДЫДУЩИЙ%s"),*Name,
                Respawn>0?*FString::Printf(TEXT(" · ВОЗРОЖДЕНИЕ %.0f С"),FMath::CeilToFloat(Respawn)):TEXT(""));
        }
        else if(!Hero->Status->IsAlive()) Hint=GS->AvailableArenaTeeth()>0?FString::Printf(TEXT("ВОЗРОЖДЕНИЕ ЧЕРЕЗ %.0f С"),FMath::Max(0.,Hero->RespawnAt-Now)):TEXT("НЕТ ЗАПАСНЫХ ЗУБОВ");
        float Contact=Hero->ContactProgress;
        if (const auto* Mimic=Hero->MimicCaptor.Get(); IsValid(Mimic))
        {
            Hint=TEXT("ТЕБЯ ПРОГЛОТИЛ МИМИК · ТОВАРИЩ ДОЛЖЕН УДЕРЖИВАТЬ E У СУНДУКА");
            Contact=Mimic->RescueProgress();
        }
        else if (Hero->Status->IsAlive())
        {
            const AMCRewardChest* Nearest=nullptr;
            double Distance=DBL_MAX;
            for (TActorIterator<AMCRewardChest> It(GetWorld());It;++It) if (It->CanRescue(Hero))
            {
                const double Candidate=FVector::DistSquared(Hero->GetActorLocation(),It->GetActorLocation());
                if (Candidate<Distance) { Nearest=*It; Distance=Candidate; }
            }
            if (Nearest)
            {
                const auto* Captive=Nearest->GetCapturedPlayer();
                const auto* Player=Captive?Captive->GetPlayerState():nullptr;
                const FString Name=Player?Player->GetPlayerName():TEXT("товарища");
                Hint=FString::Printf(TEXT("УДЕРЖИВАЙ E · ВЫТАЩИТЬ %s ИЗ МИМИКА"),*Name);
                Contact=Nearest->RescueProgress();
            }
        }
        if(Hero->CanWork()) for(TActorIterator<AMCFogBrawlEvent> It(GetWorld());It;++It)
        {
            if(!It->IsActive()) continue;
            if(It->Stage==EMCFogBrawlStage::Warning)
            {
                const bool Guarding=It->IsGuarding(Hero);
                FString Direction;
                if(IsValid(It->ActiveTarget))
                {
                    FVector ViewLocation; FRotator ViewRotation=Hero->GetControlRotation();
                    if(const auto* Owner=GetOwningPlayer()) Owner->GetPlayerViewPoint(ViewLocation,ViewRotation);
                    const FVector Offset=(It->ActiveTarget->GetActorLocation()-Hero->GetActorLocation()).GetSafeNormal2D();
                    FVector Forward=ViewRotation.Vector().GetSafeNormal2D();
                    if(Forward.IsNearlyZero()) Forward=FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Z).GetSafeNormal2D();
                    const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward);
                    const float Side=FVector::DotProduct(Offset,Right),Ahead=FVector::DotProduct(Offset,Forward);
                    Direction=FMath::Abs(Side)>FMath::Abs(Ahead)?Side>0?TEXT("СПРАВА"):TEXT("СЛЕВА")
                        :Ahead>0?TEXT("ВПЕРЕДИ"):TEXT("СЗАДИ");
                }
                Hint=Guarding
                    ?FString::Printf(TEXT("ДЕРЖИ ПКМ · ЗАЩИТНИКОВ %d · УДАР ЧЕРЕЗ %.1f С"),It->GuardCount,FMath::Max(0.,It->WarningEndsAt-Now))
                    :FString::Printf(TEXT("НАЙДИ ЗУБ %s · РЯДОМ ДЕРЖИ ПКМ · %.1f С"),*Direction,FMath::Max(0.,It->WarningEndsAt-Now));
            }
            else if(It->Stage==EMCFogBrawlStage::Recovery)
                Hint=It->LastImpactAt<0?TEXT("СОБЕРИТЕСЬ · СЛЕДУЮЩИЙ УДАР ЖДЁТ ГОТОВНОСТИ КОМАНДЫ")
                    :It->LastGuardCount>0?FString::Printf(TEXT("ЗУБ СПАСЁН · %d ЗАЩИТНИКОВ · %.0f УРОНА КАЖДОМУ"),It->LastGuardCount,It->LastDamagePerGuard)
                    :TEXT("ЗУБ ВЫБИТ · СОБЕРИТЕСЬ К СЛЕДУЮЩЕМУ УДАРУ");
            else Hint=TEXT("ТУМАН СГУЩАЕТСЯ · ИЩИ ПУЛЬСИРУЮЩИЙ КРАСНЫМ ЗУБ");
            break;
        }
        Text(TEXT("ActionHint"),Hint); Show(TEXT("ContactProgress"),Contact>0); Bar(TEXT("ContactProgress"),Contact);
    }
    if(auto* Results=Find(TEXT("ResultsPanel")))
        Results->SetRenderTranslation(FVector2D(Directed && HUD::DirectorDebug.GetValueOnGameThread()>0?-186.f:0.f,0));
    Show(TEXT("ResultsPanel"),Finished); Text(TEXT("ResultTitle"),Title);
    Text(TEXT("ResultDetail"),FString::Printf(TEXT("Незавершённых событий: %d   ·   R — новый забег"),GS->FailedEvents));
    // Keep the actual equipped tools and controls during the lesson; its own
    // fairy card supplies the objective instead of the ordinary day overview.
    const bool Overview=!GS->bTutorialActive;
    Show(TEXT("ObjectivesPanel"),Overview);
    Show(TEXT("TimerPanel"),Overview);
    Show(TEXT("PlayersPanel"),Overview);
    if (!Overview) {
        Show(TEXT("EventsPanel"),false);
        Show(TEXT("ResultsPanel"),false);
        if (DirectorPanel) DirectorPanel->SetVisibility(ESlateVisibility::Collapsed);
        if (DirectorCandidatesPanel) DirectorCandidatesPanel->SetVisibility(ESlateVisibility::Collapsed);
    }
}
