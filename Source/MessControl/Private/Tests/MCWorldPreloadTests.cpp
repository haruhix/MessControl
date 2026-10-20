#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "MCPreloadManifest.h"
#include "MCDataAssets.h"
#include "MCDayPlan.h"
#include "MCFoodCollisionData.h"
#include "MCGameplayUITheme.h"
#include "MCHubTypes.h"
#include "MCSingleDayDirector.h"
#include "Blueprint/UserWidget.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPreloadFoodTableDependenciesTest,
    "MessControl.Loading.Manifest.FoodTableDependenciesAndDeduplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPreloadFoodTableDependenciesTest::RunTest(const FString&)
{
    auto* Plan=NewObject<UMCDayPlan>();
    Plan->CoffeeProfile.Reset();
    Plan->ColdColaProfile.Reset();
    auto* Menu=NewObject<UDataTable>(Plan);
    Menu->RowStruct=FMCFoodRow::StaticStruct();
    Plan->Menu=Menu;

    FMCFoodRow Row;
    TArray<FSoftObjectPath> ExpectedMeshes;
    for(int32 Index=0;Index<20;++Index) {
        const FSoftObjectPath Path(FString::Printf(TEXT("/Game/Tests/Loading/SM_Food_%02d.SM_Food_%02d"),Index,Index));
        ExpectedMeshes.Add(Path);
        Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>(Path));
        Row.FragmentMeshes.Add(TSoftObjectPtr<UStaticMesh>(Path));
    }
    Row.WholeMeshes.Add(TSoftObjectPtr<UStaticMesh>());
    const TSoftObjectPtr<UStaticMesh> DuplicateFragment=Row.FragmentMeshes[0];
    Row.FragmentMeshes.Add(DuplicateFragment);
    auto* Collision=NewObject<UMCFoodCollisionData>(Menu);
    const FSoftObjectPath CollisionSource(TEXT("/Game/Tests/Loading/SM_CollisionSource.SM_CollisionSource"));
    Collision->SourceMesh=TSoftObjectPtr<UStaticMesh>(CollisionSource);
    Row.CollisionData={Collision,Collision,nullptr};
    Menu->AddRow(TEXT("FirstIngredient"),Row);
    Menu->AddRow(TEXT("SecondIngredient"),Row);

    TArray<FSoftObjectPath> Paths;
    FMCPreloadManifest::CollectSoftReferences(Plan,Paths);
    TestEqual(TEXT("The first async pass requests the menu, not its unloaded dependencies"),Paths.Num(),1);
    TestTrue(TEXT("The authored menu is retained as a root"),Paths.Contains(FSoftObjectPath(Menu)));

    FMCPreloadManifest::CollectSoftReferences(Menu,Paths);
    TestEqual(TEXT("Rows, repeated arrays and shared hard data assets produce one path per dependency"),Paths.Num(),22);
    for(const auto& Path:ExpectedMeshes)
        TestTrue(FString::Printf(TEXT("All twenty food variants are discovered: %s"),*Path.ToString()),Paths.Contains(Path));
    TestTrue(TEXT("A hard referenced collision data asset contributes its soft source mesh"),Paths.Contains(CollisionSource));
    TestFalse(TEXT("An optional empty mesh never becomes a load request"),Paths.Contains(FSoftObjectPath()));
    FMCPreloadManifest::CollectSoftReferences(Menu,Paths);
    TestEqual(TEXT("Expanding the same root again is idempotent"),Paths.Num(),22);
    for(const auto& Path:ExpectedMeshes)
        TestNull(TEXT("Collecting dependencies never synchronously loads missing meshes"),Path.ResolveObject());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPreloadExcludedDestinationsAndAudioTest,
    "MessControl.Loading.Manifest.ExcludesDestinationWorldsAndStreamedAudio",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPreloadExcludedDestinationsAndAudioTest::RunTest(const FString&)
{
    auto* Destinations=NewObject<UDataTable>();
    Destinations->RowStruct=FMCHubDestinationRow::StaticStruct();
    FMCHubDestinationRow Destination;
    const FSoftObjectPath MapPath(TEXT("/Game/Tests/Loading/L_UnselectedDay.L_UnselectedDay"));
    const FSoftObjectPath ProfilePath(TEXT("/Game/Tests/Loading/DA_SelectedDay.DA_SelectedDay"));
    Destination.Map=TSoftObjectPtr<UWorld>(MapPath);
    Destination.SingleDayProfile=TSoftObjectPtr<UMCSingleDayProfile>(ProfilePath);
    Destinations->AddRow(TEXT("SelectedDay"),Destination);

    TArray<FSoftObjectPath> Paths;
    FMCPreloadManifest::CollectSoftReferences(Destinations,Paths);
    TestEqual(TEXT("A destination row contributes its day profile only"),Paths.Num(),1);
    TestTrue(TEXT("The day profile remains eligible for the next async pass"),Paths.Contains(ProfilePath));
    TestFalse(TEXT("The destination world must not be loaded into the current world"),Paths.Contains(MapPath));

    auto* Audio=NewObject<UMCSoundPalette>();
    Audio->Events.Reset();Audio->Scenes.Reset();
    FMCSoundScene Scene;
    const FSoftObjectPath MusicPath(TEXT("/Game/Tests/Loading/SW_LongMusic.SW_LongMusic"));
    const FSoftObjectPath AmbiencePath(TEXT("/Game/Tests/Loading/SW_LongAmbience.SW_LongAmbience"));
    Scene.Music.Sound=TSoftObjectPtr<USoundBase>(MusicPath);
    Scene.Ambience.Sound=TSoftObjectPtr<USoundBase>(AmbiencePath);
    Audio->Scenes.Add(TEXT("Gameplay"),Scene);
    FMCPreloadManifest::CollectSoftReferences(Audio,Paths);
    TestEqual(TEXT("Visual preparation does not accumulate scene audio from nested map values"),Paths.Num(),1);
    TestFalse(TEXT("Long music uses the audio component's own stream request"),Paths.Contains(MusicPath));
    TestFalse(TEXT("Ambience does not hold the visual loading barrier"),Paths.Contains(AmbiencePath));
    FMCPreloadManifest::CollectSoftReferences(nullptr,Paths);
    TestEqual(TEXT("An optional null root preserves existing dependencies"),Paths.Num(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPreloadUIContainerDependenciesTest,
    "MessControl.Loading.Manifest.UIMapValuesAndSoftWidgetClasses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPreloadUIContainerDependenciesTest::RunTest(const FString&)
{
    auto* Theme=NewObject<UMCGameplayUITheme>();
    Theme->BodyFont=FSlateFontInfo();Theme->HeadingFont=FSlateFontInfo();
    Theme->DisplayFont=FSlateFontInfo();Theme->CompactHeadingFont=FSlateFontInfo();
    Theme->LocalizationTable.Reset();Theme->BoosterTable.Reset();Theme->CardTable.Reset();
    Theme->BossPortraits.Reset();Theme->WidgetClasses.Reset();
    const FSoftObjectPath Portrait(TEXT("/Game/Tests/Loading/T_BossPortrait.T_BossPortrait"));
    const FSoftObjectPath WidgetClass(TEXT("/Game/Tests/Loading/WBP_Reward.WBP_Reward_C"));
    Theme->BossPortraits.Add(TEXT("Knight"),TSoftObjectPtr<UTexture2D>(Portrait));
    Theme->BossPortraits.Add(TEXT("Wizard"),TSoftObjectPtr<UTexture2D>(Portrait));
    Theme->BossPortraits.Add(TEXT("Optional"),TSoftObjectPtr<UTexture2D>());
    Theme->WidgetClasses.Add(TEXT("Reward"),TSoftClassPtr<UUserWidget>(WidgetClass));
    Theme->WidgetClasses.Add(TEXT("Optional"),TSoftClassPtr<UUserWidget>());

    TArray<FSoftObjectPath> Paths;
    FMCPreloadManifest::CollectSoftReferences(Theme,Paths);
    TestEqual(TEXT("Soft map values include each portrait and generated widget class exactly once"),Paths.Num(),2);
    TestTrue(TEXT("Portrait map values are traversed"),Paths.Contains(Portrait));
    TestTrue(TEXT("Soft widget class values retain their generated class suffix"),Paths.Contains(WidgetClass));
    TestNull(TEXT("The collector does not instantiate the widget class"),WidgetClass.ResolveObject());
    // Reflection can encounter a malformed hard-data reference. It must not turn
    // a local preparation request into unbounded recursion or mutate that data.
    Theme->BodyFont.FontObject=Theme;
    FMCPreloadManifest::CollectSoftReferences(Theme,Paths);
    TestEqual(TEXT("A cyclic hard data reference terminates without duplicate requests"),Paths.Num(),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPreloadNativeClassDefaultsTest,
    "MessControl.Loading.Manifest.NativeClassDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPreloadNativeClassDefaultsTest::RunTest(const FString&)
{
    const auto* Defaults=GetDefault<UMCDayPlan>();
    const FSoftObjectPath Menu=Defaults->Menu.ToSoftObjectPath();
    const FSoftObjectPath Coffee=Defaults->CoffeeProfile.ToSoftObjectPath();
    const FSoftObjectPath Cola=Defaults->ColdColaProfile.ToSoftObjectPath();
    TArray<FSoftObjectPath> Paths;
    FMCPreloadManifest::CollectSoftReferences(UMCDayPlan::StaticClass(),Paths);
    TestTrue(TEXT("A loaded native class contributes its constructor-defined menu"),!Menu.IsNull() && Paths.Contains(Menu));
    TestTrue(TEXT("Constructor-defined coffee resources are available before the event spawns"),!Coffee.IsNull() && Paths.Contains(Coffee));
    TestTrue(TEXT("Constructor-defined cola resources are available before the event spawns"),!Cola.IsNull() && Paths.Contains(Cola));
    TestEqual(TEXT("Collecting a class default leaves its menu reference unchanged"),Defaults->Menu.ToSoftObjectPath(),Menu);
    return true;
}

#endif
