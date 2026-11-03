#if WITH_DEV_AUTOMATION_TESTS
#include "MCUniversalUIWidget.h"
#include "MCGameplayUIFactory.h"
#include "MCGameUserSettings.h"
#include "MCUIDataTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUniversalUICompositionTest,"MessControl.UI.Universal.CompositionAndPreviewIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCUniversalUICompositionTest::RunTest(const FString&)
{
    const auto BeforeVersion=UMCGameplayUIFactory::GetPresentationVersion();
    const FString BeforeLanguage=FInternationalization::Get().GetCurrentLanguage()->GetName();
    auto* Current=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::Current);
    const FLinearColor BeforeSurface=Current->Palette.Surface;
    TStrongObjectPtr<UMCUniversalUIPreviewWidget> Sheet(NewObject<UMCUniversalUIPreviewWidget>());
    Sheet->SetDesignerFlags(EWidgetDesignFlags::Designing);Sheet->PreviewCulture=TEXT("en");
    Sheet->Initialize();Sheet->BuildPresentationTree();
    auto* Sample=Sheet->WidgetTree->FindWidget<UMCUniversalUIWidget>(TEXT("PreviewProgress"));
    if(!TestNotNull(TEXT("Specimen contains the real reusable component"),Sample)) return false;
    auto* Table=NewObject<UDataTable>(Sheet.Get());Table->RowStruct=FMCUILocalizationRow::StaticStruct();
    FMCUILocalizationRow Row;Row.RU=FText::FromString(TEXT("Русский"));Row.EN=FText::FromString(TEXT("English text"));
    Table->AddRow(TEXT("Test.NestedLabel"),Row);Sheet->TranslationTable=Table;
    Sample->LabelKey=TEXT("Test.NestedLabel");Sample->Progress=.37f;
    Sheet->ApplyEditorPreview(0);
    Sample->SetProgressValue(.37f);
    TestEqual(TEXT("Nested component inherits the root preview culture"),Sample->Localized(TEXT("Test.NestedLabel"),FText()).ToString(),FString(TEXT("English text")));
    auto* Root=Sample->WidgetTree->RootWidget.Get();
    auto* Custom=Sheet->WidgetTree->ConstructWidget<UTextBlock>();Custom->SetText(FText::FromString(TEXT("Owned by parent")));
    Sample->SetContentForSlot(TEXT("Content"),Custom);
    for(auto Version:{EMCUIVersion::New,EMCUIVersion::Current,EMCUIVersion::New})
    {
        Sheet->bOverridePreviewUIVersion=true;Sheet->PreviewUIVersion=Version;Sheet->ApplyTheme();
        TestTrue(TEXT("Profile changes preserve the composed slot"),Sample->GetContentForSlot(TEXT("Content"))==Custom);
        TestTrue(TEXT("Profile changes retain the widget tree"),Sample->WidgetTree->RootWidget==Root);
        TestEqual(TEXT("Profile changes preserve the presented progress"),Sample->Progress,.37f);
        TestTrue(TEXT("Nested component follows the selected preview profile"),Sample->GetResolvedTheme()==UMCGameplayUIFactory::GetThemeForVersion(Version));
    }
    TestTrue(TEXT("Current theme is not edited by preview"),Current->Palette.Surface==BeforeSurface);
    Sheet->bOverridePreviewUIVersion=false;
    Sheet->Theme=UMCGameplayUIFactory::GetThemeForVersion(EMCUIVersion::New);
    TestTrue(TEXT("Palette-editor selection is independent of the runtime profile"),Sheet->GetResolvedTheme()==Sheet->Theme);
    TestEqual(TEXT("Preview does not select a runtime profile"),UMCGameplayUIFactory::GetPresentationVersion(),BeforeVersion);
    TestEqual(TEXT("Preview does not switch the editor language"),FInternationalization::Get().GetCurrentLanguage()->GetName(),BeforeLanguage);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUIPreferenceIsolationTest,"MessControl.UI.Universal.PreferenceWriteIsolation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCUIPreferenceIsolationTest::RunTest(const FString&)
{
    auto* Settings=UMCGameUserSettings::Get();
    if(!TestNotNull(TEXT("Local preferences are available"),Settings) || !GConfig) return false;
    const auto Before=UMCGameplayUIFactory::GetPresentationVersion();
    const float BeforeVolume=Settings->GetMasterVolume();
    const FString Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Automation/UI"));
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Filename=FPaths::Combine(Directory,FGuid::NewGuid().ToString()+TEXT(".ini"));
    {
        TGuardValue<FString> Ini(GGameUserSettingsIni,Filename);
        const FString Section=Settings->GetClass()->GetPathName();
        auto& File=GConfig->Add(Filename,FConfigFile());File.bCanSaveAllSections=true;
        GConfig->SetString(*Section,TEXT("Untouched"),TEXT("keep"),Filename);
        Settings->SetMasterVolume(.17f); // A pending settings-page change, deliberately not applied/saved.
        const auto Other=Before==EMCUIVersion::Current?EMCUIVersion::New:EMCUIVersion::Current;
        TestTrue(TEXT("F3 accepts and persists its local choice"),UMCGameplayUIFactory::SetPresentationVersion(Other,true));
        FString Value;
        TestTrue(TEXT("The selected UI key is saved"),GConfig->GetString(*Section,TEXT("UIPresentationVersion"),Value,Filename));
        TestEqual(TEXT("UI key has the selected enum value"),Value,StaticEnum<EMCUIVersion>()->GetNameStringByValue(int64(Other)));
        TestFalse(TEXT("F3 does not save a pending volume change"),GConfig->GetString(*Section,TEXT("MasterVolume"),Value,Filename));
        TestTrue(TEXT("Unrelated existing ini values are retained"),GConfig->GetString(*Section,TEXT("Untouched"),Value,Filename) && Value==TEXT("keep"));
        Settings->SetMasterVolume(BeforeVolume);
        UMCGameplayUIFactory::SetPresentationVersion(Before,false);
        GConfig->UnloadFile(Filename);
    }
    IFileManager::Get().Delete(*Filename);
    return true;
}
#endif
