#include "MCLoadingWidget.h"

#include "MCPlayerController.h"
#include "MCPreloadSettings.h"
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
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Throbber.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
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
}

void UMCLoadingWidget::NativeConstruct()
{
    Super::NativeConstruct();
    BindPresentationTree();
    // Keep the full-screen artwork opaque: only the controls fade over it.
    FooterOpacity=IsDesignTime()?1.f:0.f;
    if(Footer) Footer->SetRenderOpacity(FooterOpacity);
}

void UMCLoadingWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    // ScaleToFit normalizes against the previous allotted area. A newly sized
    // PIE/travel window can therefore crop the first frame. This full-screen
    // art has a known extent: compute its uniform fit from this frame's actual
    // viewport geometry and prepass it before its children are painted.
    if(!IsDesignTime() && Artwork && WidgetTree)
        if(auto* Fit=WidgetTree->FindWidget<UScaleBox>(TEXT("LoadingArtFit"))) {
            const FVector2D View=Geometry.GetLocalSize();
            const FVector2D Source(Artwork->GetBrush().ImageSize);
            if(View.X>0 && View.Y>0 && Source.X>0 && Source.Y>0) {
                const float Scale=float(FMath::Min(View.X/Source.X,View.Y/Source.Y));
                if(Fit->GetStretch()!=EStretch::UserSpecified || !FMath::IsNearlyEqual(Fit->GetUserSpecifiedScale(),Scale,.0001f)) {
                    Fit->SetStretch(EStretch::UserSpecified);Fit->SetUserSpecifiedScale(Scale);Fit->ForceLayoutPrepass();
                }
            }
        }
    if(Footer && FooterOpacity<1.f) {
        FooterOpacity=FMath::FInterpTo(FooterOpacity,1.f,DeltaSeconds,8.f);
        if(FooterOpacity>.995f) FooterOpacity=1.f;
        Footer->SetRenderOpacity(FooterOpacity);
    }
    // Cosmetic easing only follows measured work and never advances readiness.
    if(bHasMeasuredProgress && DisplayedProgress<TargetProgress) {
        DisplayedProgress=FMath::Min(TargetProgress,FMath::FInterpTo(DisplayedProgress,TargetProgress,DeltaSeconds,9.f));
        if(TargetProgress-DisplayedProgress<.001f) DisplayedProgress=TargetProgress;
        RefreshPresentation();
    }
}

void UMCLoadingWidget::BuildPresentationTree()
{
    if(!WidgetTree) return;
    if(WidgetTree->RootWidget) {BindPresentationTree();return;}
    auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("LoadingRoot"));
    WidgetTree->RootWidget=Canvas;
    auto* Backdrop=WidgetTree->ConstructWidget<UBorder>();
    Backdrop->SetBrush(FSlateColorBrush(FLinearColor(.16f,.055f,.065f,1.f)));
    auto* BackdropSlot=Canvas->AddChildToCanvas(Backdrop);
    BackdropSlot->SetAnchors(FAnchors(0,0,1,1));BackdropSlot->SetOffsets(FMargin(0));

    auto* ArtFit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("LoadingArtFit"));
    ArtFit->SetStretch(EStretch::ScaleToFit);
    auto* ArtSlot=Canvas->AddChildToCanvas(ArtFit);
    ArtSlot->SetAnchors(FAnchors(0,0,1,1));ArtSlot->SetOffsets(FMargin(0));
    Artwork=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("LoadingArtwork"));
    ArtFit->AddChild(Artwork);
    auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("LoadingFooterWidth"));
    Width->SetWidthOverride(400.f);Width->SetMaxDesiredHeight(448.f);Width->SetClipping(EWidgetClipping::ClipToBounds);
    auto* Center=Canvas->AddChildToCanvas(Width);
    Center->SetAnchors(FAnchors(0,0));Center->SetAlignment(FVector2D(0,0));
    Center->SetPosition(FVector2D(32,32));Center->SetAutoSize(true);
    auto* Panel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("LoadingFooterPanel"));
    Panel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.012f,.027f,.034f,.84f),18.f));
    Panel->SetPadding(FMargin(18.f,14.f));Width->AddChild(Panel);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Panel->AddChild(Column);
    auto MakeText=[&](const TCHAR* Name,int32 FontSize,FLinearColor Color,float Bottom) {
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
        Label->SetFont(GetResolvedTheme()->MakeGameplayFont(FontSize));
        Label->SetColorAndOpacity(Color);Label->SetAutoWrapText(true);
        Label->SetWrapTextAt(364.f);Label->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
        Column->AddChildToVerticalBox(Label)->SetPadding(FMargin(0,0,0,Bottom));return Label;
    };
    Title=MakeText(TEXT("LoadingTitle"),22,FLinearColor(.93f,.98f,.96f),6.f);
    Status=MakeText(TEXT("LoadingStatus"),16,FLinearColor(.80f,.91f,.92f),5.f);
    Detail=MakeText(TEXT("LoadingDetail"),12,FLinearColor(.60f,.75f,.78f),8.f);
    ProgressLabel=MakeText(TEXT("LoadingAssetLabel"),13,FLinearColor(.67f,.87f,.87f),8.f);
    AssetProgress=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("LoadingAssetProgress"));
    FProgressBarStyle ProgressStyle;
    ProgressStyle.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(.012f,.035f,.045f),4.f));
    ProgressStyle.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White,4.f));
    ProgressStyle.SetMarqueeImage(FSlateRoundedBoxBrush(FLinearColor(.48f,.78f,.70f),4.f));
    AssetProgress->SetWidgetStyle(ProgressStyle);
    AssetProgress->SetFillColorAndOpacity(FLinearColor(.48f,.78f,.70f));
    auto* BarSize=WidgetTree->ConstructWidget<USizeBox>();BarSize->SetHeightOverride(8.f);BarSize->AddChild(AssetProgress);
    Column->AddChildToVerticalBox(BarSize)->SetPadding(FMargin(0,0,0,4.f));
    auto* Spinner=WidgetTree->ConstructWidget<UThrobber>(UThrobber::StaticClass(),TEXT("LoadingSpinner"));
    Spinner->SetNumberOfPieces(3);Spinner->SetAnimateHorizontally(false);
    Spinner->SetAnimateVertically(true);Spinner->SetAnimateOpacity(false);
    auto Piece=Spinner->GetImage();Piece.TintColor=FLinearColor(.48f,.78f,.70f);Spinner->SetImage(Piece);
    Column->AddChildToVerticalBox(Spinner)->SetHorizontalAlignment(HAlign_Left);
    auto* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();Column->AddChildToVerticalBox(Actions);
    auto MakeButton=[&](const TCHAR* Name,UTextBlock*& OutLabel) {
        auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Name);
        FButtonStyle Style;
        Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.055f,.12f,.14f),10.f));
        Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.09f,.20f,.21f),10.f));
        Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.035f,.08f,.10f),10.f));
        Style.SetNormalPadding(FMargin(16,10));Style.SetPressedPadding(FMargin(16,10));Button->SetStyle(Style);
        OutLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*(FString(Name)+TEXT("Label"))));
        OutLabel->SetFont(GetResolvedTheme()->MakeGameplayFont(15));
        OutLabel->SetAutoWrapText(true);OutLabel->SetWrapTextAt(190.f);
        OutLabel->SetColorAndOpacity(FLinearColor(.91f,.98f,.97f));Button->AddChild(OutLabel);
        Actions->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0,0,12.f,0));return Button;
    };
    UTextBlock* RetryText=nullptr;RetryButton=MakeButton(TEXT("RetryLoading"),RetryText);RetryLabel=RetryText;
    UTextBlock* ReturnText=nullptr;ReturnButton=MakeButton(TEXT("LeaveLoading"),ReturnText);ReturnLabel=ReturnText;
    BindPresentationTree();
    RefreshPresentation();
}

void UMCLoadingWidget::BindPresentationTree()
{
    if(!WidgetTree || !WidgetTree->RootWidget || BoundPresentationRoot.Get()==WidgetTree->RootWidget) return;
    BoundPresentationRoot=WidgetTree->RootWidget;
    Footer=WidgetTree->FindWidget<UBorder>(TEXT("LoadingFooterPanel"));
    Artwork=WidgetTree->FindWidget<UImage>(TEXT("LoadingArtwork"));
    if(Artwork) {
        UObject* Resource=Artwork->GetBrush().GetResourceObject();
        UTexture2D* Texture=Cast<UTexture2D>(Resource);
        if(!Resource) Texture=GetDefault<UMCPreloadSettings>()->LoadingScreenArt.LoadSynchronous();
        // SetBrushFromTexture only honors MatchSize when its resource pointer changes.
        // Repair the saved size even when this already references the same texture.
        if(Texture) {
            FSlateBrush Brush=Artwork->GetBrush();
            Brush.ImageSize=FVector2f(float(Texture->GetSizeX()),float(Texture->GetSizeY()));
            Brush.SetResourceObject(Texture);
            Artwork->SetBrush(Brush);
        }
    }
    Title=WidgetTree->FindWidget<UTextBlock>(TEXT("LoadingTitle"));
    // The existing editable shell predates named footer containers. Retain it.
    if(!Footer && Title)
        if(auto* Column=Cast<UVerticalBox>(Title->GetParent())) Footer=Cast<UBorder>(Column->GetParent());
    Status=WidgetTree->FindWidget<UTextBlock>(TEXT("LoadingStatus"));
    Detail=WidgetTree->FindWidget<UTextBlock>(TEXT("LoadingDetail"));
    ProgressLabel=WidgetTree->FindWidget<UTextBlock>(TEXT("LoadingAssetLabel"));
    AssetProgress=WidgetTree->FindWidget<UProgressBar>(TEXT("LoadingAssetProgress"));
    RetryButton=WidgetTree->FindWidget<UButton>(TEXT("RetryLoading"));
    ReturnButton=WidgetTree->FindWidget<UButton>(TEXT("LeaveLoading"));
    RetryLabel=WidgetTree->FindWidget<UTextBlock>(TEXT("RetryLoadingLabel"));
    ReturnLabel=WidgetTree->FindWidget<UTextBlock>(TEXT("LeaveLoadingLabel"));
    if(RetryButton) RetryButton->OnClicked.AddUniqueDynamic(this,&UMCLoadingWidget::RetryPreparation);
    if(ReturnButton) ReturnButton->OnClicked.AddUniqueDynamic(this,&UMCLoadingWidget::ReturnToMenu);
}

FText UMCLoadingWidget::Text(FName Key,const FText& Russian,const FText& English) const
{
    const FString Culture=IsDesignTime()?PreviewCulture:FInternationalization::Get().GetCurrentLanguage()->GetName();
    const bool RussianCulture=Culture.StartsWith(TEXT("ru"),ESearchCase::IgnoreCase);
    return UMCUIDataLibrary::ResolveText(GetResolvedTranslationTable(),Key,RussianCulture?Russian:English,IsDesignTime()?PreviewCulture:FString());
}

void UMCLoadingWidget::SetPreparation(UMCWorldPreloadSubsystem* Value) {Preparation=Value;RefreshPresentation();}
void UMCLoadingWidget::SetWaitingForPlayers(bool bWaiting) {bWaitingForPlayers=bWaiting;RefreshPresentation();}
void UMCLoadingWidget::SetWaitingDetail(const FText& Value) {WaitingDetail=Value;RefreshPresentation();}

void UMCLoadingWidget::RefreshPresentation()
{
    BindPresentationTree();
    const bool Preview=IsDesignTime();
    const bool Failed=Preview?PreviewState==TEXT("Failed"):Preparation && Preparation->HasFailed();
    const bool Waiting=Preview?(PreviewState==TEXT("Waiting") || PreviewScenario==EMCGameplayUIPreviewScenario::Waiting):bWaitingForPlayers;
    const EMCWorldPreloadPhase Phase=Preview
        ?PreviewState==TEXT("Graphics")?EMCWorldPreloadPhase::PreparingRendering
        :PreviewState==TEXT("Streaming")?EMCWorldPreloadPhase::WaitingForStreaming
        :PreviewState==TEXT("Connecting")?EMCWorldPreloadPhase::Idle:EMCWorldPreloadPhase::LoadingAssets
        :Preparation?Preparation->GetPhase():EMCWorldPreloadPhase::Idle;
    if(Title) Title->SetText(Failed?Text(TEXT("loading.failed"),LOCTEXT("FailedRu","Не удалось загрузить локацию"),LOCTEXT("FailedEn","Unable to prepare this location"))
        :Waiting?Text(TEXT("loading.waiting"),LOCTEXT("WaitingRu","Ждём остальных игроков"),LOCTEXT("WaitingEn","Waiting for the other players"))
        :Text(TEXT("loading.title"),LOCTEXT("TitleRu","Подготавливаем локацию"),LOCTEXT("TitleEn","Preparing the location")));
    FText PhaseStatus=Preparation?Preparation->GetStatusText()
        :Text(TEXT("loading.starting"),LOCTEXT("StartingRu","Начинаем загрузку…"),LOCTEXT("StartingEn","Starting preparation…"));
    if(Preview && !Failed) {
        if(Phase==EMCWorldPreloadPhase::LoadingAssets)
            PhaseStatus=Text(TEXT("loading.phase.assets"),LOCTEXT("PreviewAssetsRu","Загружаем ресурсы локации"),LOCTEXT("PreviewAssetsEn","Loading level resources"));
        else if(Phase==EMCWorldPreloadPhase::PreparingRendering)
            PhaseStatus=Text(TEXT("loading.phase.graphics"),LOCTEXT("PreviewGraphicsRu","Подготавливаем графику"),LOCTEXT("PreviewGraphicsEn","Preparing graphics"));
        else if(Phase==EMCWorldPreloadPhase::WaitingForStreaming)
            PhaseStatus=Text(TEXT("loading.phase.streaming"),LOCTEXT("PreviewStreamingRu","Подготавливаем текстуры и модели"),LOCTEXT("PreviewStreamingEn","Preparing textures and meshes"));
    }
    if(Status) Status->SetText(Waiting
        ?Text(TEXT("loading.localready"),LOCTEXT("LocalReadyRu","У тебя всё готово."),LOCTEXT("LocalReadyEn","Everything is ready on your side."))
        :Failed?FText::GetEmpty():PhaseStatus);
    if(Detail) Detail->SetText(Failed && Preparation?Preparation->GetDetailText():Waiting?WaitingDetail:FText::GetEmpty());
    if(Detail) Detail->SetVisibility(Detail->GetText().IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    const bool Measured=!Failed && !Waiting && (Preview
        ?Phase==EMCWorldPreloadPhase::LoadingAssets || Phase==EMCWorldPreloadPhase::WaitingForStreaming
        :Preparation && Preparation->HasMeasuredPhaseProgress());
    const bool ShowProgress=!Failed && !Waiting && !(Preparation && Preparation->IsReady());
    const float RawProgress=Preview?PreviewLoadingProgress:Preparation?Preparation->GetMeasuredPhaseProgress():0.f;
    TargetProgress=FMath::IsFinite(RawProgress)?FMath::Clamp(RawProgress,0.f,.99f):0.f;
    const int32 Stage=int32(Phase);
    if(Preview) DisplayedProgress=TargetProgress;
    else if(Stage!=LastProgressStage || Measured!=bHasMeasuredProgress) DisplayedProgress=0.f;
    // A newly discovered dependency can expand the denominator. Never display
    // more completed work than the current measurement even while easing.
    DisplayedProgress=FMath::Min(DisplayedProgress,TargetProgress);
    LastProgressStage=Stage;
    bHasMeasuredProgress=Measured;
    const float Progress=DisplayedProgress;
    if(AssetProgress) {
        AssetProgress->SetPercent(Progress);
        AssetProgress->SetIsMarquee(false);
        AssetProgress->SetVisibility(ShowProgress && Measured?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    }
    if(auto* Spinner=WidgetTree?WidgetTree->FindWidget<UThrobber>(TEXT("LoadingSpinner")):nullptr) {
        // Fixed Designer renders do not advance Slate's curve clock; t=0 hides
        // all pieces. Keep fixture dots static; actual loading keeps its motion.
        Spinner->SetAnimateHorizontally(false);
        Spinner->SetAnimateVertically(!Preview);
        // SThrobber's sine alpha is negative for half its cycle. Keep the dots
        // visible while their height breathes, rather than blanking the status.
        Spinner->SetAnimateOpacity(false);
        Spinner->SetVisibility(ShowProgress && !Measured?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(ProgressLabel) ProgressLabel->SetVisibility(ShowProgress && Measured?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    FFormatNamedArguments Arguments;Arguments.Add(TEXT("Percent"),FMath::FloorToInt(Progress*100.f));
    const FText ProgressFormat=Phase==EMCWorldPreloadPhase::WaitingForStreaming
        ?Text(TEXT("loading.streaming.percent"),LOCTEXT("StreamingPercentRu","Текстуры и модели: {Percent}%"),LOCTEXT("StreamingPercentEn","Textures and meshes: {Percent}%"))
        :Text(TEXT("loading.assets"),LOCTEXT("AssetsRu","Ресурсы: {Percent}%"),LOCTEXT("AssetsEn","Assets: {Percent}%"));
    if(ProgressLabel) ProgressLabel->SetText(UMCUIDataLibrary::FormatTextForCulture(ProgressFormat,Arguments,IsDesignTime()?PreviewCulture:FString()));
    if(RetryLabel) RetryLabel->SetText(Text(TEXT("loading.retry"),LOCTEXT("RetryRu","Повторить"),LOCTEXT("RetryEn","Retry")));
    if(ReturnLabel) ReturnLabel->SetText(Text(TEXT("loading.return"),LOCTEXT("ReturnRu","В главное меню"),LOCTEXT("ReturnEn","Return to main menu")));
    if(RetryButton) RetryButton->SetVisibility(Failed?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(ReturnButton) ReturnButton->SetVisibility(Failed || Waiting?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    const FString Signature=FString::Printf(TEXT("%.4f|%d|%d|%d|%s|%s|%s"),Progress,Failed,Waiting,Measured,
        Title?*Title->GetText().ToString():TEXT(""),Status?*Status->GetText().ToString():TEXT(""),Detail?*Detail->GetText().ToString():TEXT(""));
    if(Signature!=LastPresentationSignature) {
        LastPresentationSignature=Signature;
        OnLoadingPresentationChanged(Progress,Failed,Waiting);
    }
}

void UMCLoadingWidget::ApplyEditorPreview(float Time)
{
    Super::ApplyEditorPreview(Time);
    RefreshPresentation();
}

void UMCLoadingWidget::OnLocalizationChanged()
{
    Super::OnLocalizationChanged();
    RefreshPresentation();
}

UWidget* UMCLoadingWidget::GetFocusTarget() const
{
    return Preparation && Preparation->HasFailed() && RetryButton?static_cast<UWidget*>(RetryButton):const_cast<UMCLoadingWidget*>(this);
}

void UMCLoadingWidget::RetryPreparation()
{
    if(IsDesignTime()) return;
    if(Preparation && Preparation->HasFailed()) {bWaitingForPlayers=false;Preparation->Retry();RefreshPresentation();}
}

void UMCLoadingWidget::ReturnToMenu()
{
    if(IsDesignTime()) return;
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
