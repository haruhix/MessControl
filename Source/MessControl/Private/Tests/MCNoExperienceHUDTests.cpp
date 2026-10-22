#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "MCGameplayHUD.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNoExperienceHUDTest,
    "MessControl.UI.HUD.ChestOnlyRewardsRetiresExperience",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FMCNoExperienceHUDTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::GamePreview,false);
    if(!TestNotNull(TEXT("Preview world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::GamePreview).SetCurrentWorld(World);
    {
        TStrongObjectPtr<UMCGameplayHUD> HUD(NewObject<UMCGameplayHUD>(World));
        HUD->WidgetTree=NewObject<UWidgetTree>(HUD.Get(),TEXT("WidgetTree"));
        HUD->SetDesignerFlags(EWidgetDesignFlags::Designing);
        HUD->BuildPresentationTree();
        auto* Tree=HUD->WidgetTree.Get();
        TestNull(TEXT("A fresh HUD never constructs the retired experience panel"),Tree->FindWidget(TEXT("TeamProgression")));
        TestNotNull(TEXT("Cleanliness remains present"),Tree->FindWidget(TEXT("CleanProgress")));
        TestNotNull(TEXT("Task completion remains present"),Tree->FindWidget(TEXT("TaskProgress")));
        auto* Root=Cast<UCanvasPanel>(Tree->RootWidget);
        if(TestNotNull(TEXT("Native HUD root"),Root))
        {
            // Model a saved WBP from before XP was removed. Its artist widgets
            // and object identities must survive the runtime/Designer update.
            auto* Legacy=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("TeamProgression"));
            Root->AddChildToCanvas(Legacy);
            auto* Column=Tree->ConstructWidget<UVerticalBox>();Legacy->SetContent(Column);
            auto* Label=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ProgressionText"));
            Label->SetText(FText::FromString(TEXT("Team level 2 / 75 XP")));Column->AddChildToVerticalBox(Label);
            auto* Bar=Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("ProgressionBar"));
            Bar->SetPercent(.5f);Column->AddChildToVerticalBox(Bar);
            auto* Artist=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ArtistDecoration"));
            Root->AddChildToCanvas(Artist);
            HUD->BuildPresentationTree();HUD->ApplyTheme();HUD->ApplyEditorPreview(.5f);
            TestTrue(TEXT("Saved XP panel remains collapsed even in Designer preview"),Legacy->GetVisibility()==ESlateVisibility::Collapsed);
            TestTrue(TEXT("Legacy label is collapsed and empty"),Label->GetVisibility()==ESlateVisibility::Collapsed && Label->GetText().IsEmpty());
            TestTrue(TEXT("Legacy gauge is collapsed and zeroed"),Bar->GetVisibility()==ESlateVisibility::Collapsed && Bar->GetPercent()==0.f);
            TestTrue(TEXT("Saved panel object is preserved"),Tree->FindWidget(TEXT("TeamProgression"))==Legacy);
            TestTrue(TEXT("Unrelated artist decoration is preserved"),Tree->FindWidget(TEXT("ArtistDecoration"))==Artist && Artist->GetVisibility()==ESlateVisibility::Visible);
        }
    }
    GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}

#endif
