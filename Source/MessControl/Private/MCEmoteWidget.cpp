#include "MCEmoteWidget.h"
#include "MCPlayerController.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCExpressionComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "InputKeyEventArgs.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace MCEmotePrivate
{
    const FLinearColor White(.98f,.97f,.93f),Muted(.56f,.66f,.72f);
    const TCHAR* Categories[]={TEXT("МИМИКА"),TEXT("ЖЕСТЫ"),TEXT("СИГНАЛЫ")};
    FVector2D WheelCenter(const FGeometry& Geometry) {return Geometry.GetLocalSize()*.5f+FVector2D(-165,-10);}
    UTextBlock* Label(UWidgetTree* Tree,const FText& Text,int32 Size)
    {
        auto* Result=Tree->ConstructWidget<UTextBlock>();Result->SetText(Text);
        Result->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),Size));Result->SetColorAndOpacity(FSlateColor(White));
        Result->SetJustification(ETextJustify::Center);return Result;
    }
}
UMCEmoteButton::UMCEmoteButton() {InitIsFocusable(false);}
void UMCEmoteButton::Configure(AMCPlayerController* Player,FName Emote)
{
    Controller=Player;Id=Emote;OnClicked.AddUniqueDynamic(this,&UMCEmoteButton::Clicked);
}
void UMCEmoteButton::ConfigureAlarm(AMCPlayerController* Player,EMCPlayerAlarm Value)
{
    Configure(Player,NAME_None);Alarm=Value;
}
void UMCEmoteButton::ConfigureColor(AMCPlayerController* Player,FLinearColor Value)
{
    Configure(Player,NAME_None);bColorChoice=true;PlayerColor=Value;
}
void UMCEmoteButton::Clicked()
{
    if(!Controller) return;
    if(bColorChoice) {Controller->ServerSetPlayerColor(PlayerColor);return;}
    if(Alarm!=EMCPlayerAlarm::None) Controller->ServerSendAlarm(Alarm);
    else if(auto* Hero=Cast<AMCToothCharacter>(Controller->GetPawn());Hero && Hero->Expression) Hero->Expression->ServerPlayEmote(Id);
    Controller->ToggleEmotes();
}
void UMCEmoteWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();SetIsFocusable(true);
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
    auto* Border=WidgetTree->ConstructWidget<UBorder>();Border->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,16));
    Border->SetBrushColor(FLinearColor(.025f,.045f,.06f,.98f));Border->SetPadding(FMargin(20));
    auto* Panel=Root->AddChildToCanvas(Border);Panel->SetAnchors(FAnchors(.5f,.5f));Panel->SetAlignment(FVector2D(0,.5f));
    Panel->SetPosition(FVector2D(80,-10));Panel->SetSize(FVector2D(305,470));
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Border->SetContent(Box);
    CategoryTitle=MCEmotePrivate::Label(WidgetTree,FText::FromString(MCEmotePrivate::Categories[0]),21);
    Box->AddChildToVerticalBox(CategoryTitle)->SetPadding(FMargin(0,0,0,12));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Box->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Items=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Items);
    Hint=MCEmotePrivate::Label(WidgetTree,FText::GetEmpty(),13);Hint->SetAutoWrapText(true);Hint->SetColorAndOpacity(FSlateColor(MCEmotePrivate::Muted));
    Box->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0,12,0,0));
    auto* Colors=WidgetTree->ConstructWidget<UHorizontalBox>();
    auto* ColorSlot=Root->AddChildToCanvas(Colors);ColorSlot->SetAnchors(FAnchors(.5f,.5f));ColorSlot->SetAlignment(FVector2D(.5f,0));
    ColorSlot->SetPosition(FVector2D(-165,230));ColorSlot->SetSize(FVector2D(390,40));
    const FLinearColor Palette[]={FLinearColor(.24f,.65f,1),FLinearColor(1,.27f,.31f),FLinearColor(.26f,.91f,.71f),FLinearColor(.75f,.45f,1)};
    const TCHAR* ColorNames[]={TEXT("Синий"),TEXT("Коралл"),TEXT("Мята"),TEXT("Сирень")};
    for(int32 Index=0;Index<4;++Index)
    {
        auto* Button=WidgetTree->ConstructWidget<UMCEmoteButton>();Button->ConfigureColor(Cast<AMCPlayerController>(GetOwningPlayer()),Palette[Index]);
        Button->SetBackgroundColor(Palette[Index]);Button->SetContent(MCEmotePrivate::Label(WidgetTree,FText::FromString(ColorNames[Index]),12));
        auto* ButtonSlot=Colors->AddChildToHorizontalBox(Button);ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ButtonSlot->SetPadding(FMargin(3));
    }
}
void UMCEmoteWidget::SelectCategory(int32 Value)
{
    Category=FMath::Clamp(Value,0,2);KeyboardChoice=0;Refresh();InvalidateLayoutAndVolatility();
}
void UMCEmoteWidget::Refresh()
{
    if(!Items) return;
    Items->ClearChildren();Buttons.Reset();
    if(CategoryTitle) CategoryTitle->SetText(FText::FromString(MCEmotePrivate::Categories[Category]));
    auto* Player=Cast<AMCPlayerController>(GetOwningPlayer());
    auto Add=[&](FName Id,const FText& Label,EMCPlayerAlarm Alarm)
    {
        auto* Button=WidgetTree->ConstructWidget<UMCEmoteButton>();
        if(Alarm==EMCPlayerAlarm::None) Button->Configure(Player,Id);else Button->ConfigureAlarm(Player,Alarm);
        Button->SetBackgroundColor(FLinearColor(.08f,.25f,.23f));Button->SetContent(MCEmotePrivate::Label(WidgetTree,Label,17));
        Items->AddChildToVerticalBox(Button)->SetPadding(FMargin(0,5));Buttons.Add(Button);
    };
    if(Category==2)
    {
        Add(NAME_None,FText::FromString(TEXT("!  Внимание")),EMCPlayerAlarm::Attention);
        Add(NAME_None,FText::FromString(TEXT("+  Нужна помощь")),EMCPlayerAlarm::Help);
    }
    else if(const auto* Hero=GetOwningPlayerPawn<AMCToothCharacter>();Hero && Hero->Expression && Hero->Expression->Library)
        for(const FMCEmoteEntry& Entry:Hero->Expression->Library->Entries)
        {
            const bool FaceOnly=Entry.bFaceOnly || !Entry.Animation;
            if(FaceOnly==(Category==0)) Add(Entry.Id,Entry.Label,EMCPlayerAlarm::None);
        }
    KeyboardChoice=FMath::Clamp(KeyboardChoice,0,FMath::Max(0,Buttons.Num()-1));RefreshAvailability();
}
void UMCEmoteWidget::RefreshAvailability()
{
    auto* Hero=GetOwningPlayerPawn<AMCToothCharacter>();
    for(int32 Index=0;Index<Buttons.Num();++Index)
    {
        auto* Button=Buttons[Index].Get();bool Enabled=Hero && Hero->Status && Hero->Status->IsAlive();
        if(Button->Alarm==EMCPlayerAlarm::None)
        {
            const auto* Library=Hero && Hero->Expression?Hero->Expression->Library.Get():nullptr;
            const auto* Entry=Library?Library->Entries.FindByPredicate([&](const FMCEmoteEntry& Value){return Value.Id==Button->Id;}):nullptr;
            Enabled=Entry && Hero->Expression->CanPlay(*Entry) && Hero->Expression->EmoteAlpha()<.01f;
        }
        Button->SetIsEnabled(Enabled);
        Button->SetBackgroundColor(Index==KeyboardChoice?FLinearColor(.12f,.39f,.34f):FLinearColor(.08f,.20f,.22f));
    }
    if(Hint) Hint->SetText(FText::FromString(Category==2?TEXT("Сигнал видят все игроки над твоим персонажем."):
        Category==1?TEXT("Жесты — стоя на земле. Движение, работа и боль прерывают жест."):TEXT("Выбери выражение лица.")));
}
void UMCEmoteWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt);if(!IsVisible()) return;
    RefreshElapsed+=Dt;if(RefreshElapsed>=.1f) {RefreshElapsed=0;RefreshAvailability();}
}
FReply UMCEmoteWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    const FKey Key=Event.GetKey();
    if(Key==EKeys::Tab)
    {
        if(auto* PC=Cast<AMCPlayerController>(GetOwningPlayer()))
        {
            PC->ToggleEmotes();
            PC->InputKey(FInputKeyEventArgs(nullptr,Event.GetInputDeviceId(),Key,IE_Pressed,Event.GetEventTimestamp()));
            PC->ShowScoreboard();
        }
        return FReply::Handled();
    }
    if(Key==EKeys::T || Key==EKeys::Escape)
    {
        if(auto* PC=Cast<AMCPlayerController>(GetOwningPlayer())) PC->ToggleEmotes();return FReply::Handled();
    }
    if(Key==EKeys::One || Key==EKeys::Two || Key==EKeys::Three)
    {SelectCategory(Key==EKeys::One?0:Key==EKeys::Two?1:2);return FReply::Handled();}
    if(Key==EKeys::Left || Key==EKeys::Right)
    {SelectCategory((Category+(Key==EKeys::Left?2:1))%3);return FReply::Handled();}
    if(Key==EKeys::Up || Key==EKeys::Down)
    {
        if(!Buttons.IsEmpty()) KeyboardChoice=(KeyboardChoice+Buttons.Num()+(Key==EKeys::Up?-1:1))%Buttons.Num();
        RefreshAvailability();return FReply::Handled();
    }
    if(Key==EKeys::Enter || Key==EKeys::SpaceBar)
    {
        if(Buttons.IsValidIndex(KeyboardChoice) && Buttons[KeyboardChoice]->GetIsEnabled()) Buttons[KeyboardChoice]->OnClicked.Broadcast();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(Geometry,Event);
}
FReply UMCEmoteWidget::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton)
    {
        const FVector2D Delta=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())-MCEmotePrivate::WheelCenter(Geometry);
        if(Delta.Size()>=65 && Delta.Size()<=210)
        {
            float Angle=FMath::RadiansToDegrees(FMath::Atan2(Delta.Y,Delta.X))+150;
            if(Angle<0) Angle+=360;if(Angle>=360) Angle-=360;
            SelectCategory(FMath::Min(2,FMath::FloorToInt(Angle/120)));return FReply::Handled();
        }
    }
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
int32 UMCEmoteWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,FLinearColor(0,0,0,.56f));
    const FVector2D Center=MCEmotePrivate::WheelCenter(Geometry);
    auto Text=[&](FVector2D Position,const FString& Value,int32 Size,FLinearColor Color)
    {
        FSlateDrawElement::MakeText(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(230,32),FSlateLayoutTransform(Position)),Value,
            FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),Size),ESlateDrawEffect::None,Color);
    };
    for(int32 Index=0;Index<3;++Index)
    {
        TArray<FVector2D> Arc;
        for(int32 Step=0;Step<=28;++Step)
        {
            const float Angle=FMath::DegreesToRadians(-148.f+Index*120+116.f*Step/28);
            Arc.Add(Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*137.f);
        }
        const FLinearColor Tint=Index==Category?FLinearColor(.08f,.38f,.32f,.98f):FLinearColor(.045f,.10f,.14f,.97f);
        FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Arc,ESlateDrawEffect::None,Tint,true,132);
        const float Mid=FMath::DegreesToRadians(-90.f+Index*120);
        const FVector2D Position=Center+FVector2D(FMath::Cos(Mid),FMath::Sin(Mid))*136.f;
        Text(Position-FVector2D(48,12),FString::Printf(TEXT("%d  %s"),Index+1,MCEmotePrivate::Categories[Index]),16,MCEmotePrivate::White);
    }
    Text(Center-FVector2D(24,13),TEXT("T"),24,MCEmotePrivate::White);
    Text(Center+FVector2D(-174,208),TEXT("ЦВЕТ ИГРОКА"),12,MCEmotePrivate::Muted);
    Text(Geometry.GetLocalSize()*.5f+FVector2D(-322,285),TEXT("1 / 2 / 3 — раздел    ↑ / ↓ — выбор    Enter — отправить    T / Esc — закрыть"),13,MCEmotePrivate::White);
    return Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer+3,Style,ParentEnabled);
}
