#include "MCPlayerNameComponent.h"
#include "MCToothCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerState.h"
#include "Styling/CoreStyle.h"

void UMCPlayerNameWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetVisibility(ESlateVisibility::HitTestInvisible);
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
    Background->SetContent(NameText);
    WidgetTree->RootWidget=Background;
}
void UMCPlayerNameWidget::SetPlayerName(const FString& Name)
{
    if(NameText && NameText->GetText().ToString()!=Name) NameText->SetText(FText::FromString(Name));
}
FString UMCPlayerNameWidget::GetPlayerName() const
{
    return NameText?NameText->GetText().ToString():FString();
}
UMCPlayerNameComponent::UMCPlayerNameComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    SetTickMode(ETickMode::Enabled);
    SetWidgetSpace(EWidgetSpace::Screen);
    SetWidgetClass(UMCPlayerNameWidget::StaticClass());
    SetDrawSize(FVector2D(260,38));
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
    if(auto* Label=Cast<UMCPlayerNameWidget>(GetUserWidgetObject())) Label->SetPlayerName(Name);
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
