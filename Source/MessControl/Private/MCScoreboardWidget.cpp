#include "MCScoreboardWidget.h"
#include "MCGameplayHUD.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/GameStateBase.h"
#include "Styling/CoreStyle.h"

namespace MCScoreboardPrivate
{
    UTextBlock* Label(UWidgetTree* Tree,const FString& Text,int32 Size)
    {
        auto* Result=Tree->ConstructWidget<UTextBlock>();
        Result->SetText(FText::FromString(Text)); Result->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),Size));
        Result->SetColorAndOpacity(FSlateColor(FLinearColor(.98f,.97f,.93f)));
        return Result;
    }
    USizeBox* Width(UWidgetTree* Tree,UWidget* Child,float Value)
    {
        auto* Result=Tree->ConstructWidget<USizeBox>();Result->SetWidthOverride(Value);Result->SetContent(Child);return Result;
    }
}
void UMCScoreboardWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized(); SetVisibility(ESlateVisibility::HitTestInvisible);
    if(WidgetTree->RootWidget) return;
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=Root;
    auto* Background=WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,18));
    Background->SetBrushColor(FLinearColor(.012f,.020f,.033f,.96f));Background->SetPadding(FMargin(24));
    auto* Panel=Root->AddChildToCanvas(Background);Panel->SetAnchors(FAnchors(.5f,.5f));Panel->SetAlignment(FVector2D(.5f,.5f));Panel->SetSize(FVector2D(660,420));
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Background->SetContent(Box);
    Box->AddChildToVerticalBox(MCScoreboardPrivate::Label(WidgetTree,TEXT("КОМАНДА"),25))->SetPadding(FMargin(0,0,0,16));
    auto* Header=WidgetTree->ConstructWidget<UHorizontalBox>();
    Box->AddChildToVerticalBox(Header)->SetPadding(FMargin(0,0,0,10));
    Header->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,MCScoreboardPrivate::Label(WidgetTree,TEXT(""),14),46));
    Header->AddChildToHorizontalBox(MCScoreboardPrivate::Label(WidgetTree,TEXT("ИГРОК"),14))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Header->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,MCScoreboardPrivate::Label(WidgetTree,TEXT("ОЧКИ"),14),90));
    Header->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,MCScoreboardPrivate::Label(WidgetTree,TEXT("PING"),14),80));
    auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Box->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Players=WidgetTree->ConstructWidget<UVerticalBox>();Scroll->AddChild(Players);
    auto* Footer=MCScoreboardPrivate::Label(WidgetTree,TEXT("Корона — хост комнаты   ·   Отпусти Tab, чтобы закрыть"),13);
    Footer->SetColorAndOpacity(FSlateColor(FLinearColor(.56f,.66f,.72f)));Box->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,16,0,0));
}
void UMCScoreboardWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt); if(!IsVisible()) return; RefreshElapsed+=Dt;
    if(RefreshElapsed>=.25f) {RefreshElapsed=0;Refresh();}
}
void UMCScoreboardWidget::RebuildRows(int32 Count)
{
    if(!Players) return;
    Players->ClearChildren(); Rows.Reset();
    for(int32 Index=0;Index<Count;++Index)
    {
        auto* Background=WidgetTree->ConstructWidget<UBorder>();Background->SetPadding(FMargin(8));
        Background->SetBrushColor(FLinearColor(.04f,.075f,.10f,.8f));
        Players->AddChildToVerticalBox(Background)->SetPadding(FMargin(0,3));
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Background->SetContent(Row);
        FMCScoreboardRow& Controls=Rows.AddDefaulted_GetRef();
        Controls.Icon=CreateWidget<UMCHUDIcon>(GetOwningPlayer(),UMCHUDIcon::StaticClass());Controls.Icon->bTooth=true;
        auto* Portrait=MCScoreboardPrivate::Width(WidgetTree,Controls.Icon,38);Portrait->SetHeightOverride(42);
        Row->AddChildToHorizontalBox(Portrait)->SetPadding(FMargin(0,0,10,0));
        Controls.Name=MCScoreboardPrivate::Label(WidgetTree,TEXT(""),17);Controls.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        auto* NameSlot=Row->AddChildToHorizontalBox(Controls.Name);NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameSlot->SetVerticalAlignment(VAlign_Center);
        Controls.Points=MCScoreboardPrivate::Label(WidgetTree,TEXT("0"),19);
        Row->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,Controls.Points,90))->SetVerticalAlignment(VAlign_Center);
        Controls.Ping=MCScoreboardPrivate::Label(WidgetTree,TEXT("—"),16);
        Row->AddChildToHorizontalBox(MCScoreboardPrivate::Width(WidgetTree,Controls.Ping,80))->SetVerticalAlignment(VAlign_Center);
    }
}
void UMCScoreboardWidget::Refresh()
{
    PlayerEntries.Reset();
    const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
    if(GS)
    {
        TArray<APlayerState*> States;
        for(const auto& State:GS->PlayerArray) if(IsValid(State)) States.Add(State.Get());
        States.Sort([](const APlayerState& A,const APlayerState& B)
        {
            const auto* Left=Cast<AMCPlayerState>(&A);const auto* Right=Cast<AMCPlayerState>(&B);
            const int32 LP=Left?Left->Points:FMath::RoundToInt(A.GetScore()),RP=Right?Right->Points:FMath::RoundToInt(B.GetScore());
            return LP!=RP?LP>RP:A.GetPlayerId()<B.GetPlayerId();
        });
        for(const APlayerState* State:States)
        {
            FMCScoreboardEntry& Entry=PlayerEntries.AddDefaulted_GetRef();
            const auto* Player=Cast<AMCPlayerState>(State);const auto* Hero=Cast<AMCToothCharacter>(State->GetPawn());
            Entry.Nickname=State->GetPlayerName();Entry.Points=Player?Player->Points:FMath::RoundToInt(State->GetScore());
            Entry.PingMilliseconds=FMath::Max(0.f,State->GetPingInMilliseconds());
            Entry.bHost=Player && Player->bSessionHost;
            Entry.PlayerColor=Player?Player->PlayerColor:Hero?Hero->GetPlayerColor():FLinearColor::White;
            Entry.bSpectating=!Hero || !Hero->Status || !Hero->Status->IsAlive();
        }
    }
    if(Rows.Num()!=PlayerEntries.Num()) RebuildRows(PlayerEntries.Num());
    for(int32 Index=0;Index<Rows.Num();++Index)
    {
        const FMCScoreboardEntry& Entry=PlayerEntries[Index];FMCScoreboardRow& Row=Rows[Index];
        Row.Name->SetText(FText::FromString(Entry.Nickname+(Entry.bSpectating?TEXT(" · наблюдает"):TEXT(""))));
        Row.Points->SetText(FText::AsNumber(Entry.Points));
        Row.Ping->SetText(FText::FromString(Entry.bHost?TEXT("0 мс"):FString::Printf(TEXT("%d мс"),FMath::RoundToInt(Entry.PingMilliseconds))));
        Row.Icon->Tint=Entry.PlayerColor;Row.Icon->bHost=Entry.bHost;Row.Icon->bDead=Entry.bSpectating;Row.Icon->InvalidateLayoutAndVolatility();
    }
    OnScoreboardUpdated();
}
