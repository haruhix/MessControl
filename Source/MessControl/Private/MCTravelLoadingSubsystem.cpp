#include "MCTravelLoadingSubsystem.h"

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

void UMCTravelLoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
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
    Screen.WidgetLoadingScreen=SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(.008f,.018f,.025f,1.f))
        .HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(480.f)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,20)
                    [SNew(STextBlock).Text(FText::AsCultureInvariant(TEXT("MESSCONTROL")))
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),26)).ColorAndOpacity(FLinearColor(.44f,.86f,.80f))]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,20)
                    [SNew(STextBlock).Text(Title).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),18))]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SThrobber)]
            ]
        ];
    Player->SetupLoadingScreen(Screen);
}
