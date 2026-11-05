#include "MCPlayerNameComponent.h"
#include "MCToothCharacter.h"
#include "MCGameplayHUD.h"
#include "MCGameplayUIFactory.h"
#include "MCPlayerState.h"
#include "MCUIVisualStyle.h"
#include "MCWorldUIShapes.h"
#include "Styling/CoreStyle.h"
#include "MCVoiceIndicatorComponent.h"
#include "Blueprint/WidgetTree.h"
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

#define LOCTEXT_NAMESPACE "MCPlayerName"

UMCPlayerNameWidget::UMCPlayerNameWidget() {bUseVersionedWorldPresentation=true;}

void UMCPlayerNameWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(false);SetVisibility(ESlateVisibility::HitTestInvisible);
    BuildPresentationTree();
    ApplyTheme();
}

void UMCPlayerNameWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(NameText) {BuildWorldPresentationTree();return;}
    if(WidgetTree->RootWidget)
    {
        NameText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("PlayerNameText")));
        NameBackground=Cast<UBorder>(WidgetTree->FindWidget(TEXT("PlayerNameBackground")));
        AlarmText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("PlayerAlarmText")));
        AlarmPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("PlayerAlarmPanel")));
        PlayerIcon=Cast<UMCHUDIcon>(WidgetTree->FindWidget(TEXT("PlayerNameIcon")));
        BuildWorldPresentationTree();return;
    }
    auto* Box=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PlayerNameRoot"));WidgetTree->RootWidget=Box;
    AlarmPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("PlayerAlarmPanel"));
    AlarmPanel->SetPadding(FMargin(12,6));AlarmPanel->SetVisibility(ESlateVisibility::Collapsed);
    Box->AddChildToVerticalBox(AlarmPanel)->SetPadding(FMargin(0,0,0,6));
    AlarmText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("PlayerAlarmText"));
    AlarmText->SetJustification(ETextJustify::Center);AlarmText->SetAutoWrapText(true);AlarmText->SetWrapTextAt(296);
    AlarmText->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);AlarmText->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);AlarmPanel->SetContent(AlarmText);
    NameBackground=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("PlayerNameBackground"));NameBackground->SetPadding(FMargin(9,4));
    NameText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("PlayerNameText"));
    NameText->SetJustification(ETextJustify::Center);NameText->SetAutoWrapText(false);
    NameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    auto* NameRow=WidgetTree->ConstructWidget<UHorizontalBox>();NameBackground->SetContent(NameRow);
    PlayerIcon=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("PlayerNameIcon"));PlayerIcon->bTooth=true;
    auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(32);IconSize->SetHeightOverride(32);IconSize->SetContent(PlayerIcon);
    auto* IconSlot=NameRow->AddChildToHorizontalBox(IconSize);IconSlot->SetPadding(FMargin(0,0,6,0));IconSlot->SetVerticalAlignment(VAlign_Center);
    auto* NameWidth=WidgetTree->ConstructWidget<USizeBox>();NameWidth->SetMaxDesiredWidth(240);NameWidth->SetContent(NameText);
    NameRow->AddChildToHorizontalBox(NameWidth)->SetVerticalAlignment(VAlign_Center);
    Box->AddChildToVerticalBox(NameBackground)->SetHorizontalAlignment(HAlign_Center);
    BuildWorldPresentationTree();
}

void UMCPlayerNameWidget::BuildWorldPresentationTree()
{
    if(!bUseVersionedWorldPresentation || !WidgetTree) return;
    auto* NewRoot=WidgetTree->FindWidget<USizeBox>(TEXT("WorldNameV2Root"));
    if(!NewRoot) {
        auto* NewTheme=GetNewWorldPresentationTheme();
        NewRoot=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldNameV2Root"),320);
        auto* Stack=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("WorldNameV2Stack"));NewRoot->SetContent(Stack);
        auto* Alarm=MCWorldUIShapes::Panel(WidgetTree,TEXT("WorldNameV2Alarm"),NewTheme,EMCUIColorToken::Accent,FMargin(10,5));
        Stack->AddChildToVerticalBox(Alarm)->SetPadding(FMargin(0,0,0,5));
        auto* AlarmCopy=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldNameV2AlarmCopy"),296);Alarm->SetContentForSlot(TEXT("Content"),AlarmCopy);
        auto* Plate=MCWorldUIShapes::Panel(WidgetTree,TEXT("WorldNameV2Plate"),NewTheme,EMCUIColorToken::Cream,FMargin(10,5));
        Stack->AddChildToVerticalBox(Plate);
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("WorldNameV2Row"));Plate->SetContentForSlot(TEXT("Content"),Row);
        auto* Icon=WidgetTree->ConstructWidget<UMCHUDIcon>(UMCHUDIcon::StaticClass(),TEXT("WorldNameV2Icon"));Icon->bTooth=true;
        auto* IconBounds=MCWorldUIShapes::Bounds(WidgetTree,TEXT("WorldNameV2IconBounds"),32,32);IconBounds->SetHeightOverride(32);IconBounds->SetContent(Icon);
        auto* IconCell=Row->AddChildToHorizontalBox(IconBounds);IconCell->SetVerticalAlignment(VAlign_Center);IconCell->SetPadding(FMargin(0,0,7,0));
        auto* NameCopy=MCWorldUIShapes::Text(WidgetTree,TEXT("WorldNameV2Copy"),266);
        auto* NameCell=Row->AddChildToHorizontalBox(NameCopy);NameCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));NameCell->SetVerticalAlignment(VAlign_Center);
    }
    InstallWorldPresentation(NewRoot);
    NewNameText=WidgetTree->FindWidget<UTextBlock>(TEXT("WorldNameV2Copy"));
    NewAlarmText=WidgetTree->FindWidget<UTextBlock>(TEXT("WorldNameV2AlarmCopy"));
    NewAlarmShape=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WorldNameV2Alarm"));
    NewPlayerIcon=WidgetTree->FindWidget<UMCHUDIcon>(TEXT("WorldNameV2Icon"));
}

void UMCPlayerNameWidget::RefreshWorldPresentation()
{
    const auto* NewTheme=GetNewWorldPresentationTheme();if(!NewTheme) return;
    if(NewNameText) {
        // The reference keeps the in-world name as a light, single-line
        // caption over a translucent blue plate, separate from HUD headings.
        const auto Font=FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),34);
        NewNameText->SetFont(Font);NewNameText->SetColorAndOpacity(FLinearColor::White);
        NewNameText->SetAutoWrapText(false);NewNameText->SetWrapTextAt(0);
        NewNameText->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
        NewNameText->SetText(FText::AsCultureInvariant(PresentedName));
        MCWorldUIShapes::PrimeText(Font,{NewNameText->GetText()},1.f);
        if(FSlateApplication::IsInitialized()) if(const auto Renderer=FSlateApplication::Get().GetRenderer())
            if(auto* Bounds=WidgetTree->FindWidget<USizeBox>(TEXT("WorldNameV2Root"))) {
                const float Width=Renderer->GetFontMeasureService()->Measure(NewNameText->GetText(),Font).X;
                Bounds->SetWidthOverride(FMath::Max(110.f,FMath::CeilToFloat(Width)+59.f));
            }
    }
    if(auto* Plate=WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("WorldNameV2Plate"))) {
        const FLinearColor Body(FColor::FromHex(TEXT("1A434E"))),Rim(FColor::FromHex(TEXT("AEFCFD")));
        const auto Fill=Body.CopyWithNewOpacity(145.f/255.f),Edge=Rim.CopyWithNewOpacity(.85f);
        if(!Plate->bCustomColors || Plate->CustomFill!=Fill || Plate->CustomOutline!=Edge) {
            Plate->bCustomColors=true;Plate->CustomFill=Fill;Plate->CustomOutline=Edge;
            Plate->CustomText=FLinearColor::White;Plate->CustomShadow=FLinearColor::Transparent;
            Plate->ShadowOffset=FVector2D::ZeroVector;Plate->ApplyTheme();
        }
    }
    const bool HasAlarm=PresentedAlarm!=EMCPlayerAlarm::None;
    if(NewAlarmShape) {
        NewAlarmShape->Fill=PresentedAlarm==EMCPlayerAlarm::Help?EMCUIColorToken::Mint:EMCUIColorToken::Warning;
        NewAlarmShape->ApplyTheme();NewAlarmShape->SetVisibility(HasAlarm?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(NewAlarmText) {
        MCWorldUIShapes::TextStyle(NewAlarmText,NewTheme,18,EMCUIColorToken::Marine);
        // The source presenter owns dynamic localized copy; no duplicate alarm state/clock.
        NewAlarmText->SetText(AlarmText?AlarmText->GetText():FText::GetEmpty());
    }
    if(NewPlayerIcon) {
        NewPlayerIcon->Tint=PresentedColor;NewPlayerIcon->InkTint=NewTheme->Palette.Marine;
        NewPlayerIcon->HostTint=NewTheme->Palette.Warning;NewPlayerIcon->bHost=bPresentedHost;NewPlayerIcon->InvalidateLayoutAndVolatility();
    }
}

void UMCPlayerNameWidget::SetPlayerName(const FString& Name)
{
    PresentedName=Name;BuildPresentationTree();
    if(NameText && NameText->GetText().ToString()!=Name)
    {
        NameText->SetText(FText::AsCultureInvariant(Name));
        NameText->SetToolTipText(FText::AsCultureInvariant(Name));
    }
    if(NewNameText) NewNameText->SetText(FText::AsCultureInvariant(Name));
}

FString UMCPlayerNameWidget::GetPlayerName() const
{
    return PresentedName;
}

void UMCPlayerNameWidget::SetPresentation(FLinearColor Color,bool bHost,EMCPlayerAlarm Alarm)
{
    if(PresentedColor.Equals(Color) && bPresentedHost==bHost && PresentedAlarm==Alarm && PlayerIcon) return;
    PresentedColor=Color;bPresentedHost=bHost;PresentedAlarm=Alarm;
    ApplyTheme();
}

void UMCPlayerNameWidget::ApplyTheme()
{
    BuildPresentationTree();Super::ApplyTheme();
    const auto* Resolved=GetResolvedTheme();const auto& P=Resolved->Palette;
    MCUIVisualStyle::Panel(NameBackground,Resolved,P.Marine.CopyWithNewOpacity(P.Marine.A*.92f));
    if(NameText)
    {
        NameText->SetFont(Resolved->MakeFont(18));NameText->SetColorAndOpacity(P.OnMarine);
        NameText->SetText(FText::AsCultureInvariant(PresentedName));NameText->SetToolTipText(FText::AsCultureInvariant(PresentedName));
    }
    if(PlayerIcon)
    {
        PlayerIcon->Tint=PresentedColor;PlayerIcon->InkTint=P.Ink;PlayerIcon->HostTint=P.Warning;PlayerIcon->bHost=bPresentedHost;
        PlayerIcon->InvalidateLayoutAndVolatility();
    }
    const bool AlarmVisible=PresentedAlarm!=EMCPlayerAlarm::None;
    if(AlarmPanel)
    {
        MCUIVisualStyle::Panel(AlarmPanel,Resolved,PresentedAlarm==EMCPlayerAlarm::Help?P.Mint:P.Warning);
        AlarmPanel->SetVisibility(AlarmVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(AlarmText)
    {
        AlarmText->SetFont(Resolved->MakeFont(18));AlarmText->SetColorAndOpacity(P.Ink);
        AlarmText->SetText(!AlarmVisible?FText::GetEmpty():PresentedAlarm==EMCPlayerAlarm::Help
            ?Localized(TEXT("UI.PlayerAlarm.Help"),LOCTEXT("Help","+ НУЖНА ПОМОЩЬ"))
            :Localized(TEXT("UI.PlayerAlarm.Attention"),LOCTEXT("Attention","! ВНИМАНИЕ")));
    }
    RefreshWorldPresentation();
}

void UMCPlayerNameWidget::ApplyEditorPreview(float Time)
{
    const auto& P=GetResolvedTheme()->Palette;
    PresentedColor=P.Mint;bPresentedHost=PreviewState==TEXT("Host");
    PresentedAlarm=PreviewState==TEXT("Help")?EMCPlayerAlarm::Help:PreviewState==TEXT("Attention")?EMCPlayerAlarm::Attention:EMCPlayerAlarm::None;
    PresentedName=(PreviewScenario==EMCGameplayUIPreviewScenario::LongText?Localized(TEXT("UI.World.LongPlayerName"),LOCTEXT("LongPlayerName","Очень длинное имя игрока для проверки интерфейса")):Localized(TEXT("UI.World.PlayerName"),LOCTEXT("PlayerName","Игрок 1"))).ToString();
    Super::ApplyEditorPreview(Time);
    SetVisibility(PreviewScenario==EMCGameplayUIPreviewScenario::Empty?ESlateVisibility::Hidden:ESlateVisibility::HitTestInvisible);
}

#undef LOCTEXT_NAMESPACE


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
    if(auto* Owner=GetOwner();Owner && !Owner->FindComponentByClass<UMCVoiceIndicatorComponent>())
    {
        auto* VoiceIndicator=NewObject<UMCVoiceIndicatorComponent>(Owner,TEXT("PlayerVoiceIndicator"));
        VoiceIndicator->SetupAttachment(Owner->GetRootComponent());Owner->AddInstanceComponent(VoiceIndicator);VoiceIndicator->RegisterComponent();
    }
    if(GetWidgetClass()==UMCPlayerNameWidget::StaticClass()) SetWidgetClass(UMCGameplayUIFactory::ResolveClass(TEXT("PlayerName"),UMCPlayerNameWidget::StaticClass()));
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
