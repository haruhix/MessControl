#include "MCPlayerNameComponent.h"
#include "MCToothCharacter.h"
#include "MCGameplayHUD.h"
#include "MCPlayerState.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Styling/CoreStyle.h"

void UMCPlayerNameWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();WidgetTree->RootWidget=Box;
    AlarmPanel=WidgetTree->ConstructWidget<UBorder>();
    AlarmPanel->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,10));
    AlarmPanel->SetPadding(FMargin(12,6));AlarmPanel->SetVisibility(ESlateVisibility::Collapsed);
    Box->AddChildToVerticalBox(AlarmPanel)->SetPadding(FMargin(0,0,0,6));
    AlarmText=WidgetTree->ConstructWidget<UTextBlock>();AlarmText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),20));
    AlarmText->SetJustification(ETextJustify::Center);AlarmText->SetColorAndOpacity(FSlateColor(FLinearColor(.012f,.020f,.033f)));
    AlarmPanel->SetContent(AlarmText);
    auto* Background=WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,7.f));
    Background->SetBrushColor(FLinearColor(.012f,.020f,.033f,.80f));
    Background->SetPadding(FMargin(9,4));
    NameText=WidgetTree->ConstructWidget<UTextBlock>();
    auto Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),16);
    Font.OutlineSettings.OutlineSize=1;
    Font.OutlineSettings.OutlineColor=FLinearColor::Black;
    NameText->SetFont(Font);
    NameText->SetColorAndOpacity(FSlateColor(FLinearColor(.98f,.97f,.93f)));
    NameText->SetJustification(ETextJustify::Center);
    NameText->SetAutoWrapText(false);
    auto* NameRow=WidgetTree->ConstructWidget<UHorizontalBox>();Background->SetContent(NameRow);
    PlayerIcon=WidgetTree->ConstructWidget<UMCHUDIcon>();PlayerIcon->bTooth=true;
    auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(28);IconSize->SetHeightOverride(28);IconSize->SetContent(PlayerIcon);
    NameRow->AddChildToHorizontalBox(IconSize)->SetPadding(FMargin(0,0,6,0));
    NameRow->AddChildToHorizontalBox(NameText)->SetVerticalAlignment(VAlign_Center);
    Box->AddChildToVerticalBox(Background)->SetHorizontalAlignment(HAlign_Center);
}
void UMCPlayerNameWidget::SetPlayerName(const FString& Name)
{
    if(NameText && NameText->GetText().ToString()!=Name) NameText->SetText(FText::FromString(Name));
}
FString UMCPlayerNameWidget::GetPlayerName() const
{
    return NameText?NameText->GetText().ToString():FString();
}
void UMCPlayerNameWidget::SetPresentation(FLinearColor Color,bool bHost,EMCPlayerAlarm Alarm)
{
    if(PlayerIcon && (!PlayerIcon->Tint.Equals(Color) || PlayerIcon->bHost!=bHost))
    {PlayerIcon->Tint=Color;PlayerIcon->bHost=bHost;PlayerIcon->InvalidateLayoutAndVolatility();}
    if(!AlarmPanel || !AlarmText || PresentedAlarm==Alarm) return;
    PresentedAlarm=Alarm;
    AlarmPanel->SetVisibility(Alarm==EMCPlayerAlarm::None?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(Alarm!=EMCPlayerAlarm::None)
    {
        AlarmText->SetText(FText::FromString(Alarm==EMCPlayerAlarm::Help?TEXT("+ НУЖНА ПОМОЩЬ"):TEXT("! ВНИМАНИЕ")));
        AlarmPanel->SetBrushColor(Alarm==EMCPlayerAlarm::Help?FLinearColor(.26f,.91f,.71f):FLinearColor(1,.64f,.23f));
    }
}
UMCPlayerNameComponent::UMCPlayerNameComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    SetTickMode(ETickMode::Enabled);
    SetWidgetSpace(EWidgetSpace::Screen);
    SetWidgetClass(UMCPlayerNameWidget::StaticClass());
    SetDrawSize(FVector2D(320,90));
    SetDrawAtDesiredSize(true);
    SetPivot(FVector2D(.5f,1.f));
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetCanEverAffectNavigation(false);
    SetCastShadow(false);
    SetVisibility(false);
}
void UMCPlayerNameComponent::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()==NM_DedicatedServer) { SetComponentTickEnabled(false); return; }
    if(auto* Hero=Cast<AMCToothCharacter>(GetOwner())) AddTickPrerequisiteComponent(Hero->GetMesh());
    RefreshName();
}
void UMCPlayerNameComponent::RefreshName()
{
    const auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    const auto* State=Hero?Hero->GetPlayerState():nullptr;
    // A corpse may retain its PlayerState after that player respawns.
    const FString Name=State && State->GetPawn()==Hero?State->GetPlayerName():FString();
    DisplayedName=Name;
    if(auto* Label=Cast<UMCPlayerNameWidget>(GetUserWidgetObject()))
    {
        Label->SetPlayerName(Name);
        const auto* Player=Cast<AMCPlayerState>(State);
        const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
        const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
        const EMCPlayerAlarm Alarm=Player && !Name.IsEmpty() && Now<Player->AlarmUntil?Player->Alarm:EMCPlayerAlarm::None;
        Label->SetPresentation(Player?Player->PlayerColor:Hero?Hero->GetPlayerColor():FLinearColor::White,Player && Player->bSessionHost,Alarm);
    }
    SetVisibility(!Name.IsEmpty());
    if(!Hero) return;
    const auto* Mesh=Hero->GetMesh();
    FName Head=Hero->RigBone(TEXT("gaze_head"));
    if(!Mesh->DoesSocketExist(Head)) Head=TEXT("head");
    const FVector Anchor=Mesh->DoesSocketExist(Head)?Mesh->GetSocketLocation(Head):Hero->GetActorLocation()+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    SetWorldLocation(Anchor+FVector(0,0,HeightAboveHead));
}
void UMCPlayerNameComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    RefreshName();
    Super::TickComponent(Dt,TickType,ThisTickFunction);
}
