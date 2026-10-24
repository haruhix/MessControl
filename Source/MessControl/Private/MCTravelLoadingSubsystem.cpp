#include "MCTravelLoadingSubsystem.h"

#include "MCPreloadSettings.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
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
    void Construct(const FArguments&,UTexture2D* Texture,const FText& Title)
    {
        check(IsInGameThread());
        ArtworkTexture=Texture;
        ArtworkBrush.SetResourceObject(Texture);
        ArtworkBrush.ImageSize=FVector2D(1920,1080);
        ArtworkBrush.DrawAs=Texture?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
        FooterBrush=FSlateRoundedBoxBrush(FLinearColor(.012f,.027f,.034f,.84f),18.f);
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
        ];
    }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        Collector.AddReferencedObject(ArtworkTexture);
    }
    virtual FString GetReferencerName() const override {return TEXT("SMCTravelLoadingScreen");}
private:
    TObjectPtr<UTexture2D> ArtworkTexture;
    FSlateBrush ArtworkBrush;
    FSlateBrush FooterBrush;
};
}

void UMCTravelLoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if(!IsRunningDedicatedServer() && FApp::CanEverRender())
        LoadingArtwork=GetDefault<UMCPreloadSettings>()->LoadingScreenArt.LoadSynchronous();
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
    Screen.WidgetLoadingScreen=SNew(SMCTravelLoadingScreen,LoadingArtwork.Get(),Title);
    Player->SetupLoadingScreen(Screen);
}
