#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "MCPreloadManifest.h"
#include "MCDataAssets.h"
#include "MCDayPlan.h"
#include "MCFoodCollisionData.h"
#include "MCGameplayUITheme.h"
#include "MCHubTypes.h"
#include "MCSingleDayDirector.h"
#include "MCPerkTypes.h"
#include "MCPerkVFXProfile.h"
#include "MCGameMode.h"
#include "UObject/Package.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPreloadAvailableBoostersTest,
    "MessControl.Loading.Manifest.AvailableBoostersOnlyAtRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPreloadAvailableBoostersTest::RunTest(const FString&)
{
    auto* Table=NewObject<UDataTable>();Table->RowStruct=FMCPerkDefinition::StaticStruct();
    const FSoftObjectPath ActiveIcon(TEXT("/Game/Tests/Loading/T_Available.T_Available"));
    const FSoftObjectPath OldIcon(TEXT("/Game/Tests/Loading/T_Retired.T_Retired"));
    const FSoftObjectPath ActiveVFX(TEXT("/Game/Tests/Loading/DA_AvailableVFX.DA_AvailableVFX"));
    const FSoftObjectPath OldVFX(TEXT("/Game/Tests/Loading/DA_RetiredVFX.DA_RetiredVFX"));
    FMCPerkDefinition Row;Row.bEnabled=true;Row.Icon=TSoftObjectPtr<UTexture2D>(ActiveIcon);
    Row.VFXProfile=TSoftObjectPtr<UMCPerkVFXProfile>(ActiveVFX);Table->AddRow(TEXT("Available"),Row);
    Row.bEnabled=false;Row.Icon=TSoftObjectPtr<UTexture2D>(OldIcon);
    Row.VFXProfile=TSoftObjectPtr<UMCPerkVFXProfile>(OldVFX);Table->AddRow(TEXT("Retired"),Row);
    auto* Package=CreatePackage(*FString::Printf(TEXT("/Temp/PreloadScope_%s/L_Mouth"),*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UWorld* World=UWorld::CreateWorld(EWorldType::EditorPreview,false,TEXT("L_Mouth"),Package);
    if(!TestNotNull(TEXT("Scoped location fixture exists"),World))return false;
    TArray<FSoftObjectPath> Runtime;
    FMCPreloadManifest::CollectSoftReferences(Table,Runtime,World);
    TestTrue(TEXT("Available booster icon stays in location coverage"),Runtime.Contains(ActiveIcon));
    TestTrue(TEXT("Available booster VFX stays in location coverage"),Runtime.Contains(ActiveVFX));
    TestFalse(TEXT("Retired booster icon does not enter location coverage"),Runtime.Contains(OldIcon));
    TestFalse(TEXT("Retired booster VFX does not enter location coverage"),Runtime.Contains(OldVFX));
    TArray<FSoftObjectPath> Catalog;FMCPreloadManifest::CollectSoftReferences(Table,Catalog);
    TestTrue(TEXT("Editor discovery retains the complete catalog"),Catalog.Contains(ActiveVFX)&&Catalog.Contains(OldVFX));
    TestNull(TEXT("Discovering available effects never synchronously loads them"),ActiveVFX.ResolveObject());
    auto* Mode=World->SpawnActor<AMCGameMode>();
    const FSoftObjectPath Unselected(TEXT("/Game/Tests/Loading/DA_UnselectedRun.DA_UnselectedRun"));
    Mode->SingleDayProfile=TSoftObjectPtr<UMCSingleDayProfile>(Unselected);Mode->bUseSingleDayLoop=false;
    TArray<FSoftObjectPath> Legacy;FMCPreloadManifest::CollectSoftReferences(Mode,Legacy,World);
    TestFalse(TEXT("Legacy mode does not enroll its inactive single-day option"),Legacy.Contains(Unselected));
    Mode->bUseSingleDayLoop=true;
    TArray<FSoftObjectPath> SingleDay;FMCPreloadManifest::CollectSoftReferences(Mode,SingleDay,World);
    TestTrue(TEXT("The selected single-day option remains covered before its director exists"),SingleDay.Contains(Unselected));
    World->DestroyWorld(false);Package->SetDirtyFlag(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPreloadGeometryReferencePruning,
    "MessControl.Loading.Manifest.DenseProceduralGeometryKeepsMaterialReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMCPreloadGeometryReferencePruning::RunTest(const FString&)
{
    auto* Component=NewObject<UProceduralMeshComponent>();
    auto* Material=UMaterial::GetDefaultMaterial(MD_Surface);Component->SetMaterial(0,Material);
    TArray<FSoftObjectPath> EmptyGeometry;FMCPreloadManifest::CollectSoftReferences(Component,EmptyGeometry);
    FProcMeshSection Section;Section.ProcVertexBuffer.SetNum(200000);Section.ProcIndexBuffer.SetNumZeroed(600000);
    Component->SetProcMeshSection(0,Section);
    const double Started=FPlatformTime::Seconds();
    TArray<FSoftObjectPath> DenseGeometry;FMCPreloadManifest::CollectSoftReferences(Component,DenseGeometry);
    AddInfo(FString::Printf(TEXT("Dense geometry reference collection: %.6fs"),FPlatformTime::Seconds()-Started));
    TestTrue(TEXT("Actual material reference remains discoverable with dense procedural geometry"),DenseGeometry.Contains(FSoftObjectPath(Material)));
    TestTrue(TEXT("Numeric vertex/index arrays add no dependencies or lose existing ones"),DenseGeometry==EmptyGeometry);
    return true;
}

#endif
