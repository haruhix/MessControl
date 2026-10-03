#include "MCGameplayHUD.h"
#include "MCStaminaWidget.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCMouthSurface.h"
#include "MCArenaTooth.h"
#include "MCThroat.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/GameStateBase.h"

// Unity builds combine this file with the prototype widgets and mouth renderer.
namespace MCGameplayHUDPrivate
{
const FLinearColor Ink(.012f,.020f,.033f,.90f),White(.98f,.97f,.93f),Muted(.56f,.66f,.72f),Mint(.26f,.91f,.71f),Amber(1.f,.64f,.23f),Blue(.22f,.65f,1.f);
FString StepName(EMCDayStep Step)
{
    switch(Step) {
    case EMCDayStep::BrushLesson:return TEXT("ВРЕМЯ ЧИСТИТЬ");
    case EMCDayStep::DiscardBrushes:return TEXT("ЩЁТКИ ЗА БОРТ");
    case EMCDayStep::BreakfastRain:return TEXT("ЗАВТРАК ПАДАЕТ");
    case EMCDayStep::BreakfastCleanup:return TEXT("УБРАТЬ ОСТАТКИ");
    case EMCDayStep::CoffeeWaves:return TEXT("ГОРЯЧИЙ КОФЕ");
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
}
void UMCGameplayHUD::NativeConstruct()
{
    Super::NativeConstruct();
    Widgets.Reset();
    TArray<UWidget*> All; WidgetTree->GetAllWidgets(All);
    for(auto* Widget:All) Widgets.Add(Widget->GetFName(),Widget);
    RefreshState();
}
UWidget* UMCGameplayHUD::Find(FName Name) const
{ const auto* Widget=Widgets.Find(Name); return Widget?Widget->Get():nullptr; }
void UMCGameplayHUD::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt); RefreshElapsed+=Dt;
    if(RefreshElapsed>=.1f) { RefreshElapsed=0; RefreshState(); }
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
    const bool HasStep=GS->DayPlan && GS->DayPlan->Steps.IsValidIndex(GS->StepIndex);
    const bool Finished=GS->bDayOneComplete || GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost;
    FString Title=HasStep?HUD::StepName(GS->DayPlan->Steps[GS->StepIndex].Step):GS->CurrentEvent?GS->CurrentEvent->Title.ToString():TEXT("СКОРО НАЧНЁМ");
    if(Finished) Title=GS->Phase==EMCShiftPhase::Lost?TEXT("РОТ НЕ СПАСЁН"):TEXT("ДЕНЬ ЗАВЕРШЁН");
    Text(TEXT("DayTitle"),FString::Printf(TEXT("ДЕНЬ %d"),FMath::Max(1,GS->Day))); Text(TEXT("EventTitle"),Title);
    const float Done=GS->TasksTotal>0?1-float(GS->TasksLeft)/GS->TasksTotal:0;
    int32 Surfaces=0; float Clean=0;
    for(TActorIterator<AActor> It(GetWorld());It;++It) {
        if(const auto* Patch=Cast<AMCMouthSurface>(*It);Patch && Patch->bUlcer) continue;
        if(const auto* Tooth=Cast<AMCArenaTooth>(*It);Tooth && !Tooth->IsAvailable()) continue;
        if(const auto* Care=It->FindComponentByClass<UMCToothStatusComponent>()) { ++Surfaces; Clean+=1-Care->CoffeeAmount(); }
    }
    Clean=Surfaces?Clean/Surfaces:1;
    const float Health=GS->MouthHealth/FMath::Max(1.f,GS->RunSettings.MaxMouthHealth);
    Text(TEXT("TaskValue"),FString::Printf(TEXT("%d/%d"),FMath::Max(0,GS->TasksTotal-GS->TasksLeft),GS->TasksTotal)); Bar(TEXT("TaskProgress"),Done);
    Text(TEXT("CleanValue"),FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Clean*100))); Bar(TEXT("CleanProgress"),Clean);
    Text(TEXT("HealthValue"),FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Health*100))); Bar(TEXT("HealthProgress"),Health);
    const bool Timed=GS->PhaseEndsAt>0 && !Finished && !GS->bDevManualEvents;
    const int32 Seconds=FMath::CeilToInt(GS->SecondsLeft());
    const float Duration=FMath::Max(1.f,float(GS->PhaseEndsAt-GS->StepStartedAt));
    const float Progress=Timed?FMath::Clamp(1-GS->SecondsLeft()/Duration,0.f,1.f):Done;
    Text(TEXT("TimerValue"),Timed?FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60):TEXT("--:--"));
    Text(TEXT("TimerLabel"),Finished?TEXT("ИТОГИ ДНЯ"):Timed?TEXT("ДО СЛЕД. СОБЫТИЯ"):TEXT("БЕЗ ЛИМИТА ВРЕМЕНИ")); Bar(TEXT("TimerProgress"),Finished?1:Progress);
    if(auto* T=Cast<UTextBlock>(Find(TEXT("TimerValue")))) T->SetColorAndOpacity(FSlateColor(Timed && Seconds<10?HUD::Amber:HUD::White));
    TArray<APlayerState*> Players; for(const auto& Player:GS->PlayerArray) if(IsValid(Player)) Players.Add(Player.Get());
    Players.Sort([](const APlayerState& A,const APlayerState& B){return A.GetPlayerId()<B.GetPlayerId();});
    if(auto* Strip=Find(TEXT("PlayersPanel"))) Strip->SetRenderTranslation(FVector2D(76*(4-FMath::Min(4,Players.Num())),0));
    for(int32 I=0;I<4;++I) {
        const FString N=FString::Printf(TEXT("Player%d"),I+1); Show(FName(N),Players.IsValidIndex(I)); if(!Players.IsValidIndex(I)) continue;
        const auto* Tooth=Cast<AMCToothCharacter>(Players[I]->GetPawn()); const bool Dead=Tooth && !Tooth->Status->IsAlive();
        const auto* State=Cast<AMCPlayerState>(Players[I]);
        const FLinearColor PlayerTint=State?State->PlayerColor:Tooth?Tooth->GetPlayerColor():HUD::White;
        Color(FName(N+TEXT("Frame")),Dead?PlayerTint.Desaturate(.8f)*.6f:PlayerTint);
        if(auto* Icon=Cast<UMCHUDIcon>(Find(FName(N+TEXT("Face"))))) {
            Icon->Tint=PlayerTint;Icon->bHost=State && State->bSessionHost;
            Icon->bDead=Dead; Icon->bSad=Tooth && (Now-Tooth->TaskFailureAt<2.5 || Tooth->Status->State.Health<Tooth->Status->State.MaxHealth*.35f);
            Icon->bHappy=Tooth && Now-Tooth->TaskSuccessAt<2 && !Icon->bSad; Icon->InvalidateLayoutAndVolatility();
        }
        Bar(FName(N+TEXT("Health")),Tooth?Tooth->Status->State.Health/FMath::Max(1.f,Tooth->Status->State.MaxHealth):0);
    }
    for(int32 I=0;I<3;++I) {
        const FString N=FString::Printf(TEXT("Timeline%d"),I); const int32 Index=FMath::Max(0,GS->StepIndex-1)+I;
        const bool Visible=HasStep?GS->DayPlan->Steps.IsValidIndex(Index):I==0; Show(FName(N),Visible); if(!Visible) continue;
        const bool Past=HasStep && Index<GS->StepIndex,Current=!HasStep || Index==GS->StepIndex,Failed=Past && GS->PreviousStepFailed;
        Text(FName(N+TEXT("Label")),Past?Failed?TEXT("ПРОШЛО · НЕ ПОЛНОСТЬЮ"):TEXT("ВЫПОЛНЕНО"):Current?TEXT("СЕЙЧАС"):TEXT("ДАЛЕЕ"));
        Text(FName(N+TEXT("Title")),HasStep?HUD::StepName(GS->DayPlan->Steps[Index].Step):Title);
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
        const float Cool=Inv->SpraySecondsLeft(); Show(TEXT("SprayCooldown"),Cool>0);
        Text(TEXT("CooldownValue"),FString::Printf(TEXT("%.1f"),Cool)); Bar(TEXT("CooldownProgress"),1-Cool/Inv->CooldownSeconds());
        FString Hint=Inv->Selected==EMCToolSlot::Pickaxe?TEXT("ЛКМ · ДРОБИТЬ ТВЁРДОЕ"):Inv->Selected==EMCToolSlot::Knife?TEXT("ЛКМ · РЕЗАТЬ МЯГКОЕ"):Inv->Selected==EMCToolSlot::Spray?TEXT("УДЕРЖИВАЙ ЛКМ · ЛЕЧИТЬ ЯЗВУ"):Hero->HasBrush()?TEXT("ЛКМ · ЧИСТИТЬ"):TEXT("E · ПОДОБРАТЬ ЩЁТКУ");
        if(Hero->FoodCollection->bCollecting) Hint=FString::Printf(TEXT("СТОПКА %d/6 · НЕСИ В ЗОНУ ГЛОТКИ · Q БРОСИТЬ"),Hero->FoodCollection->Pieces.Num());
        else if(Inv->IsCleaningTool() && Hero->FoodCollection->HasCandidate()) Hint=TEXT("КЛИК ЛКМ · СОБИРАТЬ СТОПКУ");
        if(Hero->IsYawning()) Hint=TEXT("ЗЕВАНИЕ · ДЕРЖИСЬ ЗА ЯЗЫК");
        if(Hero->HeldFood) Hint=TEXT("E · ДЕРЖАТЬ     Q · БРОСИТЬ");
        if(Hero->bInCoffee) Hint=TEXT("WASD · ПЛЫТЬ     ЛКМ · ЗАЦЕПИТЬСЯ");
        if(const auto* Move=Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()); Move && Move->IsClimbing()) Hint=TEXT("WASD · ЛАЗАТЬ     E · ДЕРЖАТЬСЯ     SPACE · ОТПРЫГНУТЬ");
        for(TActorIterator<AMCThroat> It(GetWorld());It;++It) {
            if(It->CanOrderJump(Hero)) Hint=TEXT("SPACE · ПРЫГНУТЬ НА ЯЗЫЧОК");
            if(It->ContainsPlayer(Hero)) {
                if(It->ThroatPhase==EMCThroatPhase::Anticipation)
                    Hint=FString::Printf(TEXT("ДОСТАВЛЯЙ ЕЩЁ · ЗАСАСЫВАНИЕ ЧЕРЕЗ %.1f С"),FMath::Max(0.,It->PhaseStartedAt+It->AnticipationSeconds-Now));
                else if(It->ThroatPhase==EMCThroatPhase::Swallowing) Hint=TEXT("ГЛОТКА ЗАСАСЫВАЕТ · СЛЕДУЮЩАЯ ПАРТИЯ ПРИНИМАЕТСЯ");
                else if(It->ThroatPhase==EMCThroatPhase::Collecting) Hint=TEXT("ВНЕСИ СТОПКУ В ЗОНУ · ЕДА ОТПРАВИТСЯ САМА");
            }
        }
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
        Text(TEXT("ActionHint"),Hint); Show(TEXT("ContactProgress"),Hero->ContactProgress>0); Bar(TEXT("ContactProgress"),Hero->ContactProgress);
    }
    Show(TEXT("ResultsPanel"),Finished); Text(TEXT("ResultTitle"),Title);
    Text(TEXT("ResultDetail"),FString::Printf(TEXT("Незавершённых событий: %d   ·   R — новый забег"),GS->FailedEvents));
}
