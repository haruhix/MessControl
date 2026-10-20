#include "MCLoadingWidget.h"

#include "MCPlayerController.h"
#include "MCSteamSessionSubsystem.h"
#include "MCUIDataLibrary.h"
#include "MCWorldPreloadSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "MCLoading"

void UMCLoadingWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    LocalizationTable=UMCUIDataLibrary::GetDefaultLocalizationTable();
}

TSharedRef<SWidget> UMCLoadingWidget::RebuildWidget()
{
    BuildTree();
    return Super::RebuildWidget();
}

void UMCLoadingWidget::BuildTree()
{
    if(!WidgetTree || WidgetTree->RootWidget) return;
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("LoadingRoot"));
    WidgetTree->RootWidget=Canvas;
    auto* Backdrop=WidgetTree->ConstructWidget<UBorder>();
    Backdrop->SetBrush(FSlateColorBrush(FLinearColor(.008f,.018f,.025f,1.f)));
    auto* BackdropSlot=Canvas->AddChildToCanvas(Backdrop);
    BackdropSlot->SetAnchors(FAnchors(0,0,1,1));BackdropSlot->SetOffsets(FMargin(0));

    auto* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetWidthOverride(580.f);
    auto* Center=Canvas->AddChildToCanvas(Width);
    Center->SetAnchors(FAnchors(.5f,.5f));Center->SetAlignment(FVector2D(.5f,.5f));Center->SetAutoSize(true);
    auto* Panel=WidgetTree->ConstructWidget<UBorder>();
    Panel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.025f,.09f,.115f),18.f,FLinearColor(.20f,.70f,.73f),1.5f));
    Panel->SetPadding(FMargin(30.f));Width->AddChild(Panel);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Panel->AddChild(Column);
    auto MakeText=[&](const TCHAR* Name,int32 FontSize,FLinearColor Color,float Bottom) {
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),FontSize));
        Label->SetColorAndOpacity(Color);Label->SetAutoWrapText(true);
        Column->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,0,0,Bottom));return Label;
    };
    auto* Brand=MakeText(TEXT("LoadingBrand"),14,FLinearColor(.44f,.86f,.80f),16.f);
    Brand->SetText(FText::AsCultureInvariant(TEXT("MESSCONTROL")));
    Title=MakeText(TEXT("LoadingTitle"),28,FLinearColor(.93f,.98f,.96f),12.f);
    Status=MakeText(TEXT("LoadingStatus"),18,FLinearColor(.80f,.91f,.92f),10.f);
    Detail=MakeText(TEXT("LoadingDetail"),14,FLinearColor(.60f,.75f,.78f),20.f);
    ProgressLabel=MakeText(TEXT("LoadingAssetLabel"),13,FLinearColor(.67f,.87f,.87f),8.f);
    AssetProgress=WidgetTree->ConstructWidget<UProgressBar>();
    FProgressBarStyle ProgressStyle;
    ProgressStyle.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(.012f,.035f,.045f),4.f));
    ProgressStyle.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,4.f));
    AssetProgress->SetWidgetStyle(ProgressStyle);
    AssetProgress->SetFillColorAndOpacity(FLinearColor(.30f,.86f,.79f));
    auto* BarSize=WidgetTree->ConstructWidget<USizeBox>();BarSize->SetHeightOverride(8.f);BarSize->AddChild(AssetProgress);
    Column->AddChildToVerticalBox(BarSize)->SetPadding(FMargin(0,0,0,22.f));
    auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();Column->AddChildToVerticalBox(Actions);
    auto MakeButton=[&](const TCHAR* Name,UTextBlock*& OutLabel) {
        auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Name);
        FButtonStyle Style;
        Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.045f,.19f,.22f),8.f,FLinearColor(.22f,.58f,.62f),1.f));
        Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.08f,.30f,.32f),8.f,FLinearColor(.50f,.94f,.85f),1.f));
        Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.035f,.13f,.16f),8.f));
        Style.SetNormalPadding(FMargin(16,10));Style.SetPressedPadding(FMargin(16,10));Button->SetStyle(Style);
        OutLabel=WidgetTree->ConstructWidget<UTextBlock>();
        OutLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),15));
        OutLabel->SetColorAndOpacity(FLinearColor(.91f,.98f,.97f));Button->AddChild(OutLabel);
        Actions->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0,0,12.f,0));return Button;
    };
    UTextBlock* RetryText=nullptr;RetryButton=MakeButton(TEXT("RetryLoading"),RetryText);RetryLabel=RetryText;
    UTextBlock* ReturnText=nullptr;ReturnButton=MakeButton(TEXT("LeaveLoading"),ReturnText);ReturnLabel=ReturnText;
    RetryButton->OnClicked.AddUniqueDynamic(this,&UMCLoadingWidget::RetryPreparation);
    ReturnButton->OnClicked.AddUniqueDynamic(this,&UMCLoadingWidget::ReturnToMenu);
    RefreshPresentation();
}

FText UMCLoadingWidget::Text(FName Key,const FText& Russian,const FText& English) const
{
    const bool RussianCulture=FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName()==TEXT("ru");
    return UMCUIDataLibrary::ResolveText(LocalizationTable,Key,RussianCulture?Russian:English);
}

void UMCLoadingWidget::SetPreparation(UMCWorldPreloadSubsystem* Value) {Preparation=Value;RefreshPresentation();}
void UMCLoadingWidget::SetWaitingForPlayers(bool bWaiting) {bWaitingForPlayers=bWaiting;RefreshPresentation();}
void UMCLoadingWidget::SetWaitingDetail(const FText& Value) {WaitingDetail=Value;RefreshPresentation();}

void UMCLoadingWidget::RefreshPresentation()
{
    if(!Title) return;
    const bool Failed=Preparation && Preparation->HasFailed();
    Title->SetText(Failed?Text(TEXT("loading.failed"),LOCTEXT("FailedRu","Не удалось загрузить локацию"),LOCTEXT("FailedEn","Unable to prepare this location"))
        :bWaitingForPlayers?Text(TEXT("loading.waiting"),LOCTEXT("WaitingRu","Ждём остальных игроков"),LOCTEXT("WaitingEn","Waiting for the other players"))
        :Text(TEXT("loading.title"),LOCTEXT("TitleRu","Подготавливаем локацию"),LOCTEXT("TitleEn","Preparing the location")));
    Status->SetText(bWaitingForPlayers
        ?Text(TEXT("loading.localready"),LOCTEXT("LocalReadyRu","У тебя всё готово."),LOCTEXT("LocalReadyEn","Everything is ready on your side."))
        :Preparation?Preparation->GetStatusText():Text(TEXT("loading.starting"),LOCTEXT("StartingRu","Начинаем загрузку…"),LOCTEXT("StartingEn","Starting preparation…")));
    Detail->SetText(bWaitingForPlayers?WaitingDetail:Preparation?Preparation->GetDetailText():FText::GetEmpty());
    const float RawProgress=Preparation?Preparation->GetAssetProgress():0.f;
    const float Progress=FMath::IsFinite(RawProgress)?FMath::Clamp(RawProgress,0.f,1.f):0.f;
    AssetProgress->SetPercent(Progress);
    AssetProgress->SetVisibility(Failed || bWaitingForPlayers?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    ProgressLabel->SetVisibility(Failed || bWaitingForPlayers?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    FFormatNamedArguments Arguments;Arguments.Add(TEXT("Percent"),FMath::RoundToInt(Progress*100.f));
    ProgressLabel->SetText(UMCUIDataLibrary::FormatTextForCulture(Text(TEXT("loading.assets"),
        LOCTEXT("AssetsRu","Ресурсы: {Percent}%"),LOCTEXT("AssetsEn","Assets: {Percent}%")),Arguments));
    RetryLabel->SetText(Text(TEXT("loading.retry"),LOCTEXT("RetryRu","Повторить"),LOCTEXT("RetryEn","Retry")));
    ReturnLabel->SetText(Text(TEXT("loading.return"),LOCTEXT("ReturnRu","В главное меню"),LOCTEXT("ReturnEn","Return to main menu")));
    RetryButton->SetVisibility(Failed?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
}

UWidget* UMCLoadingWidget::GetFocusTarget() const
{
    return Preparation && Preparation->HasFailed()?static_cast<UWidget*>(RetryButton):const_cast<UMCLoadingWidget*>(this);
}

void UMCLoadingWidget::RetryPreparation()
{
    if(Preparation && Preparation->HasFailed()) {bWaitingForPlayers=false;Preparation->Retry();RefreshPresentation();}
}

void UMCLoadingWidget::ReturnToMenu()
{
    if(auto* Player=GetOwningPlayer<AMCPlayerController>()) {Player->ReturnToMainMenu();return;}
    if(auto* Instance=GetGameInstance()) if(auto* Steam=Instance->GetSubsystem<UMCSteamSessionSubsystem>()) {Steam->LeaveRoom();return;}
    UGameplayStatics::OpenLevel(this,TEXT("/Game/Maps/L_MainMenu"));
}

FReply UMCLoadingWidget::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::Gamepad_FaceButton_Right) {ReturnToMenu();return FReply::Handled();}
    if(Event.GetKey()==EKeys::R && Preparation && Preparation->HasFailed()) {RetryPreparation();return FReply::Handled();}
    if(Event.GetKey()==EKeys::Tab || Event.GetKey().IsGamepadKey()) return Super::NativeOnKeyDown(Geometry,Event);
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
