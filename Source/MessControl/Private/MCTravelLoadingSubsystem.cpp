#include "MCTravelLoadingSubsystem.h"

#include "MCPreloadSettings.h"
#include "MCLoadingTips.h"
#include "MCGameplayUIFactory.h"
#include "MCGameplayUITheme.h"
#include "MCUIDataLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "UObject/GCObject.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Misc/App.h"
#include "MoviePlayer.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
/** Immutable Slate data on the loading thread; asset resolution and localization happen on the game thread. */
class SMCTravelLoadingScreen : public SCompoundWidget, public FGCObject
{
public:
    SLATE_BEGIN_ARGS(SMCTravelLoadingScreen) {} SLATE_END_ARGS()
    void Construct(const FArguments&,UTexture2D* Texture,const FText& Title,const TArray<FText>& Tips,const FText& TipHeading)
    {
        check(IsInGameThread());
        ArtworkTexture=Texture;
        ArtworkBrush.SetResourceObject(Texture);
        ArtworkBrush.ImageSize=FVector2D(1920,1080);
        ArtworkBrush.DrawAs=Texture?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
        FooterBrush=FSlateRoundedBoxBrush(FLinearColor(.012f,.027f,.034f,.84f),18.f);
        TipBrush=FSlateRoundedBoxBrush(FLinearColor(.012f,.027f,.034f,.76f),18.f);
        TipTexts=Tips;
        ChildSlot
        [SNew(SOverlay)
            +SOverlay::Slot()
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                    .BorderBackgroundColor(FLinearColor(.16f,.055f,.065f))]
            +SOverlay::Slot()
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                    [SNew(SImage).Image(&ArtworkBrush)]]
            +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(32.f)
                [SNew(SBox).WidthOverride(400.f).MaxDesiredHeight(448.f)
                    [SNew(SBorder).BorderImage(&FooterBrush).Padding(FMargin(18.f,14.f))
                        [SNew(SVerticalBox)
                            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,10.f)
                                [SNew(STextBlock).Text(Title)
                                    .AutoWrapText(true).WrapTextAt(364.f)
                                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),20))
                                    .ColorAndOpacity(FLinearColor(.93f,.98f,.96f))]
                            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SThrobber).Animate(SThrobber::Vertical)]
                        ]
                    ]
                ]
            +SOverlay::Slot().Padding(32.f)
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                    [SNew(SBox).WidthOverride(760.f).MinDesiredHeight(128.f)
                        [SNew(SBorder).BorderImage(&TipBrush).Padding(FMargin(24.f,16.f)).Visibility(EVisibility::HitTestInvisible)
                            [SNew(SVerticalBox)
                                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,9.f)
                                    [SNew(STextBlock).Text(TipHeading).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14))
                                        .ColorAndOpacity(FLinearColor(.65f,.91f,.83f))]
                                +SVerticalBox::Slot().FillHeight(1.f)
                                    [SNew(SOverlay)
                                        +SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Center)
                                            [SAssignNew(TipCurrent,STextBlock).WrapTextAt(712.f).Justification(ETextJustify::Center)
                                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),20)).ColorAndOpacity(FLinearColor(.93f,.98f,.96f))]
                                        +SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Center)
                                            [SAssignNew(TipNext,STextBlock).WrapTextAt(712.f).Justification(ETextJustify::Center)
                                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),20)).ColorAndOpacity(FLinearColor(.93f,.98f,.96f))]
                                    ]
                            ]
                        ]
                    ]
                ]
        ];
        UpdateTips();
    }
    virtual void Tick(const FGeometry& Geometry,const double CurrentTime,const float DeltaTime) override
    {
        SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
        UpdateTips();
    }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        Collector.AddReferencedObject(ArtworkTexture);
    }
    virtual FString GetReferencerName() const override {return TEXT("SMCTravelLoadingScreen");}
private:
    void UpdateTips()
    {
        // Loading-thread work is limited to immutable text values and Slate.
        // No UObject access, table lookup, asset loading or gameplay clock here.
        const auto Frame=MCLoadingTips::Evaluate(FPlatformTime::Seconds(),TipTexts.Num());
        if(TipCurrent && TipTexts.IsValidIndex(Frame.Current)) {
            if(LastTipCurrent!=Frame.Current) {TipCurrent->SetText(TipTexts[Frame.Current]);LastTipCurrent=Frame.Current;}
            TipCurrent->SetRenderOpacity(Frame.CurrentOpacity);
        }
        if(TipNext && TipTexts.IsValidIndex(Frame.Next)) {
            if(LastTipNext!=Frame.Next) {TipNext->SetText(TipTexts[Frame.Next]);LastTipNext=Frame.Next;}
            TipNext->SetRenderOpacity(Frame.NextOpacity);
        }
    }
    TObjectPtr<UTexture2D> ArtworkTexture;
    FSlateBrush ArtworkBrush;
    FSlateBrush FooterBrush;
    FSlateBrush TipBrush;
    TArray<FText> TipTexts;
    TSharedPtr<STextBlock> TipCurrent,TipNext;
    int32 LastTipCurrent=INDEX_NONE,LastTipNext=INDEX_NONE;
};
}

void UMCTravelLoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if(!IsRunningDedicatedServer() && FApp::CanEverRender()) {
        LoadingArtwork=GetDefault<UMCPreloadSettings>()->LoadingScreenArt.LoadSynchronous();
        if(const auto* Theme=UMCGameplayUIFactory::GetMasterTheme()) LoadingTipTable=Theme->LocalizationTable.LoadSynchronous();
        if(!LoadingTipTable) LoadingTipTable=UMCUIDataLibrary::GetDefaultLocalizationTable();
    }
    BeforeLoadHandle=FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this,&UMCTravelLoadingSubsystem::BeforeMapLoad);
}

void UMCTravelLoadingSubsystem::Deinitialize()
{
    FCoreUObjectDelegates::PreLoadMapWithContext.Remove(BeforeLoadHandle);
    Super::Deinitialize();
}

void UMCTravelLoadingSubsystem::BeforeMapLoad(const FWorldContext& Context,const FString& MapName)
{
    // MoviePlayer is not a PIE readiness signal. Its Slate screen covers only blocking travel;
    // MCWorldPreloadSubsystem + UMCLoadingWidget cover post-load preparation and seamless travel.
    if(GIsEditor || Context.OwningGameInstance!=GetGameInstance() || IsRunningDedicatedServer()
        || !FApp::CanEverRender() || !IsMoviePlayerEnabled()) return;
    IGameMoviePlayer* Player=GetMoviePlayer();
    if(!Player || Player->IsMovieCurrentlyPlaying()) return;
    const bool Russian=FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName()==TEXT("ru");
    const FText Title=Russian?NSLOCTEXT("MCLoading","TravelRu","Загрузка локации…")
        :NSLOCTEXT("MCLoading","TravelEn","Loading the location…");
    FLoadingScreenAttributes Screen;
    Screen.bAutoCompleteWhenLoadingCompletes=true;
    Screen.bMoviesAreSkippable=false;
    Screen.bWaitForManualStop=false;
    // Blocking OpenLevel has no reliable total-progress denominator. A throbber
    // remains honest here; phase-specific percentages resume after the map exists.
    // No bound attribute performs UObject work while the game thread is blocked.
    const FString Culture=FInternationalization::Get().GetCurrentLanguage()->GetName();
    const TArray<FText> Tips=MCLoadingTips::Resolve(LoadingTipTable.Get(),Culture);
    const FText TipHeading=MCLoadingTips::Heading(LoadingTipTable.Get(),Culture);
    Screen.WidgetLoadingScreen=SNew(SMCTravelLoadingScreen,LoadingArtwork.Get(),Title,Tips,TipHeading);
    Player->SetupLoadingScreen(Screen);
}
