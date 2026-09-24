#include "MCPrototypeWidget.h"
#include "MCToothCharacter.h"
#include "MCPlayerController.h"
#include "MCGameState.h"
#include "MCToothPhysicsComponent.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Styling/CoreStyle.h"

namespace
{
    const FLinearColor Cream(0.95f,0.90f,0.77f);
    const FLinearColor Mint(0.28f,0.93f,0.75f);
    const TCHAR* TuningNames[] = {TEXT("Squash"),TEXT("Stretch"),TEXT("Run bob"),TEXT("Lean"),TEXT("Follow through"),TEXT("Tempo"),TEXT("Anticipation"),TEXT("Exaggeration")};
    const float TuningMin[] = {0,0,0,0,0,0.5f,0.05f,0.5f};
    const float TuningMax[] = {0.6f,0.6f,20,30,1,2,0.4f,2};
    const TCHAR* PhysicsNames[]={TEXT("Knockback (cm/s)"),TEXT("Lift (cm/s)"),TEXT("Fall threshold"),TEXT("Ragdoll seconds"),TEXT("Get-up seconds"),TEXT("Muscle strength"),TEXT("Muscle damping"),TEXT("Mass (kg)")};
    const float PhysicsMin[]={150,80,100,0.5f,0.25f,2,0.3f,3};
    const float PhysicsMax[]={1100,650,600,6,2,35,2,20};
}
UTextBlock* UMCPrototypeWidget::AddText(UVerticalBox* Box,const FString& Text,int32 Size,FLinearColor Color)
{
    UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
    Label->SetText(FText::FromString(Text)); Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),Size));
    Label->SetColorAndOpacity(FSlateColor(Color)); Label->SetAutoWrapText(true);
    Box->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,3)); return Label;
}
void UMCPrototypeWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized(); SetIsFocusable(true);
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Root;
    auto Panel = [&](FVector2D Position,FVector2D Size,FAnchors Anchors,FVector2D Alignment,UBorder*& Border)
    {
        Border = WidgetTree->ConstructWidget<UBorder>(); Border->SetBrushColor(FLinearColor(0.022f,0.052f,0.060f,0.94f)); Border->SetPadding(FMargin(22,16));
        auto* Slot = Root->AddChildToCanvas(Border); Slot->SetAnchors(Anchors); Slot->SetAlignment(Alignment); Slot->SetPosition(Position); Slot->SetSize(Size);
        auto* Box = WidgetTree->ConstructWidget<UVerticalBox>(); Border->SetContent(Box); return Box;
    };
    UBorder* HeaderBorder; auto* Header = Panel(FVector2D(28,28),FVector2D(440,248),FAnchors(0,0),FVector2D(0,0),HeaderBorder);
    AddText(Header,TEXT("M E S S  /  C O N T R O L"),15,Mint);
    DayLabel = AddText(Header,TEXT("DAY 01 / 07"),30,Cream);
    EventLabel = AddText(Header,TEXT("CLOCKING IN"),20,Cream);
    InstructionLabel = AddText(Header,TEXT("A little teamwork. A lot of toothpaste."),14,Cream);
    InstructionLabel->SetAutoWrapText(false); InstructionLabel->SetWrapTextAt(396.f);
    TaskLabel = AddText(Header,TEXT("Waiting for the first challenge"),15,Mint);
    UBorder* HealthBorder; auto* Health = Panel(FVector2D(-28,28),FVector2D(270,126),FAnchors(1,0),FVector2D(1,0),HealthBorder);
    HealthLabel = AddText(Health,TEXT("MOUTH HEALTH / 100"),15,Cream);
    HealthBar = WidgetTree->ConstructWidget<UProgressBar>(); HealthBar->SetFillColorAndOpacity(Mint); HealthBar->SetPercent(1); Health->AddChildToVerticalBox(HealthBar)->SetPadding(FMargin(0,8));
    TimeLabel = AddText(Health,TEXT("SHIFT STARTS IN 08"),19,Mint);
    UBorder* FooterBorder; auto* Footer = Panel(FVector2D(0,-22),FVector2D(1000,75),FAnchors(0.5f,1),FVector2D(0.5f,1),FooterBorder);
    AddText(Footer,TEXT("WASD  MOVE    SPACE  HOP    LMB  CLEAN    E  PULL / REPAIR    RMB  BONK"),15,Cream);
    AddText(Footer,TEXT("F1  TOOTH LAB      F2  PLAY WITH FRIENDS      GAMEPAD  STICK / A / RB / X / LB"),12,Mint);
    UBorder* TuningBorder; auto* Tuning = Panel(FVector2D(-28,174),FVector2D(360,710),FAnchors(1,0),FVector2D(1,0),TuningBorder); TuningPanel = TuningBorder;
    TuningScroll=WidgetTree->ConstructWidget<UScrollBox>(); TuningBorder->SetContent(TuningScroll); TuningScroll->AddChild(Tuning);
    AddText(Tuning,TEXT("TOOTH LAB"),23,Mint);
    AddText(Tuning,TEXT("Run / brush / stretch preview. Scroll for physics. F1 closes."),12,Cream);
    for (int32 Index=0; Index<8; ++Index)
    {
        SliderLabels.Add(AddText(Tuning,TuningNames[Index],14,Cream));
        USlider* Slider = WidgetTree->ConstructWidget<USlider>(); Slider->SetMinValue(TuningMin[Index]); Slider->SetMaxValue(TuningMax[Index]); Slider->SetSliderHandleColor(Mint);
        Slider->OnValueChanged.AddDynamic(this,&UMCPrototypeWidget::TuningChanged);
        Tuning->AddChildToVerticalBox(Slider)->SetPadding(FMargin(0,4,0,10)); Sliders.Add(Slider);
    }
    auto Button = [&](UVerticalBox* Box,const TCHAR* Text)
    {
        auto* B = WidgetTree->ConstructWidget<UButton>(); B->SetBackgroundColor(FLinearColor(0.08f,0.27f,0.25f));
        auto* L = WidgetTree->ConstructWidget<UTextBlock>(); L->SetText(FText::FromString(Text)); L->SetColorAndOpacity(FSlateColor(Cream)); L->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14));
        B->AddChild(L); Box->AddChildToVerticalBox(B)->SetPadding(FMargin(0,5)); return B;
    };
    Button(Tuning,TEXT("SAVE LOCAL PRESET"))->OnClicked.AddDynamic(this,&UMCPrototypeWidget::SaveClicked);
    Button(Tuning,TEXT("RESET FROM DATA ASSET"))->OnClicked.AddDynamic(this,&UMCPrototypeWidget::ResetClicked);
    SaveLabel = AddText(Tuning,TEXT("DA_ToothAnimation supplies the shared defaults."),11,Cream);
    PhysicsBox=WidgetTree->ConstructWidget<UVerticalBox>(); Tuning->AddChildToVerticalBox(PhysicsBox);
    AddText(PhysicsBox,TEXT("PHYSICS / HOST"),21,Mint);
    AddText(PhysicsBox,TEXT("Host changes apply to teeth already in this room. Spawn a practice tooth, close F1, then RMB to bonk."),12,Cream);
    Button(PhysicsBox,TEXT("SPAWN PRACTICE TOOTH"))->OnClicked.AddDynamic(this,&UMCPrototypeWidget::DummyClicked);
    Button(PhysicsBox,TEXT("TEST FALL"))->OnClicked.AddDynamic(this,&UMCPrototypeWidget::FallClicked);
    Button(PhysicsBox,TEXT("GET UP WHEN CLEAR"))->OnClicked.AddDynamic(this,&UMCPrototypeWidget::GetUpClicked);
    for (int32 I=0;I<8;++I)
    {
        PhysicsLabels.Add(AddText(PhysicsBox,PhysicsNames[I],14,Cream));
        auto* S=WidgetTree->ConstructWidget<USlider>(); S->SetMinValue(PhysicsMin[I]); S->SetMaxValue(PhysicsMax[I]); S->SetSliderHandleColor(Mint);
        S->OnValueChanged.AddDynamic(this,&UMCPrototypeWidget::PhysicsChanged);
        PhysicsBox->AddChildToVerticalBox(S)->SetPadding(FMargin(0,4,0,10)); PhysicsSliders.Add(S);
    }
    Button(PhysicsBox,TEXT("SAVE LOCAL PRESETS"))->OnClicked.AddDynamic(this,&UMCPrototypeWidget::SaveClicked);
    TuningPanel->SetVisibility(ESlateVisibility::Collapsed);
    UBorder* ConnectionBorder; auto* Connection = Panel(FVector2D(0,0),FVector2D(460,338),FAnchors(0.5f,0.5f),FVector2D(0.5f,0.5f),ConnectionBorder); ConnectionPanel = ConnectionBorder;
    AddText(Connection,TEXT("BRING YOUR MOLARS"),25,Mint);
    AddText(Connection,TEXT("Listen server / up to four players"),14,Cream);
    Button(Connection,TEXT("HOST A NEW MOUTH"))->OnClicked.AddDynamic(this,&UMCPrototypeWidget::HostClicked);
    AddText(Connection,TEXT("Join host IP address (port 7777)"),14,Cream);
    AddressBox = WidgetTree->ConstructWidget<UEditableTextBox>(); AddressBox->SetText(FText::FromString(TEXT("127.0.0.1"))); Connection->AddChildToVerticalBox(AddressBox)->SetPadding(FMargin(0,8));
    Button(Connection,TEXT("JOIN FRIEND"))->OnClicked.AddDynamic(this,&UMCPrototypeWidget::JoinClicked);
    AddText(Connection,TEXT("LAN or reachable host IP. Internet play needs UDP 7777 forwarding or a VPN. F2 / Esc closes this panel."),12,Cream);
    ConnectionPanel->SetVisibility(ESlateVisibility::Collapsed);
}
void UMCPrototypeWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    AMCGameState* State = GetWorld()->GetGameState<AMCGameState>(); if (!State || !DayLabel) return;
    const bool bWorking = State->Phase == EMCShiftPhase::Working;
    const bool bWon = State->Phase == EMCShiftPhase::Won; const bool bLost = State->Phase == EMCShiftPhase::Lost;
    DayLabel->SetText(FText::FromString(FString::Printf(TEXT("DAY %02d / 07"),FMath::Max(1,State->Day))));
    EventLabel->SetText(bWon ? FText::FromString(TEXT("SEVEN DAYS. ALL SMILES.")) : bLost ? FText::FromString(TEXT("THIS MOUTH NEEDS A BREAK")) : bWorking && State->CurrentEvent ? State->CurrentEvent->Title : FText::FromString(TEXT("TAKE A BREATHER")));
    InstructionLabel->SetText((bWon || bLost) ? FText::FromString(TEXT("Host: press R to start another shift.")) : bWorking && State->CurrentEvent ? State->CurrentEvent->Instruction : FText::FromString(TEXT("Get ready. Something messy is coming.")));
    TaskLabel->SetText(FText::FromString(bWorking ? FString::Printf(TEXT("%02d / %02d JOBS DONE    |    %d / 4 TEETH"),State->TasksTotal-State->TasksLeft,State->TasksTotal,State->PlayerArray.Num()) : FString::Printf(TEXT("%d / 4 TEETH ON DUTY"),State->PlayerArray.Num())));
    HealthLabel->SetText(FText::FromString(FString::Printf(TEXT("MOUTH HEALTH / %03d"),FMath::RoundToInt(State->MouthHealth)))); HealthBar->SetPercent(State->MouthHealth/100);
    const int32 Seconds = FMath::CeilToInt(State->SecondsLeft());
    TimeLabel->SetText(FText::FromString((bWon || bLost) ? FString(TEXT("SHIFT COMPLETE")) : bWorking ? FString::Printf(TEXT("%02d SECONDS LEFT"),Seconds) : FString::Printf(TEXT("NEXT SHIFT IN %02d"),Seconds)));
    if (LastDay != State->Day || LastPhase != static_cast<int32>(State->Phase))
    {
        if (auto* Tooth = Cast<AMCToothCharacter>(GetOwningPlayerPawn()))
            if (Tooth->SoundPalette && (bWorking || bWon || bLost)) Tooth->SoundPalette->Play(this,bWon ? TEXT("Win") : bLost ? TEXT("Lose") : TEXT("DayStart"),Tooth->GetActorLocation());
        LastDay = State->Day; LastPhase = static_cast<int32>(State->Phase);
    }
}
bool UMCPrototypeWidget::IsPanelOpen() const { return (TuningPanel && TuningPanel->IsVisible()) || (ConnectionPanel && ConnectionPanel->IsVisible()); }
bool UMCPrototypeWidget::IsTuningOpen() const { return TuningPanel && TuningPanel->IsVisible(); }
void UMCPrototypeWidget::ScrollToPhysics() { if (TuningScroll) TuningScroll->ScrollToEnd(); }
void UMCPrototypeWidget::ToggleTuning() { TuningPanel->SetVisibility(TuningPanel->IsVisible() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); ConnectionPanel->SetVisibility(ESlateVisibility::Collapsed); RefreshSliders(); }
void UMCPrototypeWidget::ToggleConnection() { ConnectionPanel->SetVisibility(ConnectionPanel->IsVisible() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); TuningPanel->SetVisibility(ESlateVisibility::Collapsed); }
FReply UMCPrototypeWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if (auto* PC = Cast<AMCPlayerController>(GetOwningPlayer()))
    {
        if (Event.GetKey()==EKeys::F1) { PC->ToggleTuning(); return FReply::Handled(); }
        if (Event.GetKey()==EKeys::F2) { PC->ToggleConnection(); return FReply::Handled(); }
        if (Event.GetKey()==EKeys::Escape) { TuningPanel->SetVisibility(ESlateVisibility::Collapsed); ConnectionPanel->SetVisibility(ESlateVisibility::Collapsed); PC->UpdateInputMode(); return FReply::Handled(); }
    }
    return Super::NativeOnKeyDown(Geometry,Event);
}
void UMCPrototypeWidget::RefreshSliders()
{
    auto* Tooth = Cast<AMCToothCharacter>(GetOwningPlayerPawn()); if (!Tooth) return;
    bRefreshing = true; const auto& A = Tooth->AnimationSettings;
    const float Values[] = {A.Squash,A.Stretch,A.Bob,A.Lean,A.FollowThrough,A.Tempo,A.Anticipation,A.Exaggeration};
    for (int32 I=0; I<Sliders.Num(); ++I) { Sliders[I]->SetValue(Values[I]); SliderLabels[I]->SetText(FText::FromString(FString::Printf(TEXT("%s  %.2f"),TuningNames[I],Values[I]))); }
    const auto& P=Tooth->ToothPhysics->Settings;
    const float PV[]={P.Knockback,P.Lift,P.FallThreshold,P.RagdollSeconds,P.GetUpSeconds,P.MuscleStrength,P.Damping,P.Mass};
    PhysicsBox->SetIsEnabled(Tooth->HasAuthority());
    for (int32 I=0;I<PhysicsSliders.Num();++I) { PhysicsSliders[I]->SetValue(PV[I]); PhysicsLabels[I]->SetText(FText::FromString(FString::Printf(TEXT("%s  %.2f"),PhysicsNames[I],PV[I]))); }
    bRefreshing = false;
}
void UMCPrototypeWidget::TuningChanged(float Value)
{
    if (bRefreshing) return;
    auto* Tooth = Cast<AMCToothCharacter>(GetOwningPlayerPawn()); if (!Tooth) return;
    auto& A = Tooth->AnimationSettings; float* Values[] = {&A.Squash,&A.Stretch,&A.Bob,&A.Lean,&A.FollowThrough,&A.Tempo,&A.Anticipation,&A.Exaggeration};
    for (int32 I=0; I<Sliders.Num(); ++I) *Values[I]=Sliders[I]->GetValue(); RefreshSliders();
}
void UMCPrototypeWidget::PhysicsChanged(float Value)
{
    auto* Tooth=Cast<AMCToothCharacter>(GetOwningPlayerPawn()); if (bRefreshing || !Tooth || !Tooth->HasAuthority()) return;
    FMCPhysicsSettings P=Tooth->ToothPhysics->Settings;
    float* Values[]={&P.Knockback,&P.Lift,&P.FallThreshold,&P.RagdollSeconds,&P.GetUpSeconds,&P.MuscleStrength,&P.Damping,&P.Mass};
    for (int32 I=0;I<8;++I) *Values[I]=PhysicsSliders[I]->GetValue();
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) It->ToothPhysics->SetTuning(P);
    RefreshSliders();
}
void UMCPrototypeWidget::FallClicked() { if (auto* T=Cast<AMCToothCharacter>(GetOwningPlayerPawn())) if (T->HasAuthority()) T->ToothPhysics->ApplyHit(T->GetActorForwardVector()*T->ToothPhysics->Settings.Knockback+FVector(0,0,T->ToothPhysics->Settings.Lift),T->GetActorLocation()); }
void UMCPrototypeWidget::GetUpClicked() { if (auto* T=Cast<AMCToothCharacter>(GetOwningPlayerPawn())) if (T->HasAuthority()) T->ToothPhysics->TryRecover(); }
void UMCPrototypeWidget::DummyClicked() { if (auto* T=Cast<AMCToothCharacter>(GetOwningPlayerPawn())) T->SpawnPracticeTooth(); }
void UMCPrototypeWidget::SaveClicked() { if (auto* Tooth = Cast<AMCToothCharacter>(GetOwningPlayerPawn())) { Tooth->SaveTuning(); if (Tooth->HasAuthority()) Tooth->ToothPhysics->SaveTuning(); SaveLabel->SetText(FText::FromString(TEXT("Saved local .ini presets in Saved/."))); } }
void UMCPrototypeWidget::ResetClicked() { if (auto* Tooth = Cast<AMCToothCharacter>(GetOwningPlayerPawn())) { Tooth->ResetTuning(); if (Tooth->HasAuthority()) { Tooth->ToothPhysics->ResetTuning(); for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) It->ToothPhysics->SetTuning(Tooth->ToothPhysics->Settings); } RefreshSliders(); } }
void UMCPrototypeWidget::HostClicked() { if (auto* PC = Cast<AMCPlayerController>(GetOwningPlayer())) PC->HostGame(); }
void UMCPrototypeWidget::JoinClicked() { if (auto* PC = Cast<AMCPlayerController>(GetOwningPlayer())) PC->JoinGame(AddressBox->GetText().ToString()); }
