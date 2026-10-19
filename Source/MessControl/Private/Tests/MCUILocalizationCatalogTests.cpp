#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "MCUIDataLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "UObject/UnrealType.h"

namespace
{
FText Copy(const TCHAR* Value) {return FText::AsCultureInvariant(Value);}
UDataTable* Catalogue()
{
    auto* Table=NewObject<UDataTable>();Table->RowStruct=FMCUILocalizationRow::StaticStruct();return Table;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUIFlatLanguagesTest,"MessControl.UI.Localization.FlatColumnsAndCultureFallback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FMCUIFlatLanguagesTest::RunTest(const FString& Parameters)
{
    UDataTable* Table=Catalogue();FMCUILocalizationRow Row;
    Row.RU=Copy(TEXT("Назад"));Row.EN=Copy(TEXT("Back"));Row.FR=Copy(TEXT("Retour"));
    Row.PT_BR=Copy(TEXT("Voltar"));Row.ZH_HANS=Copy(TEXT("返回"));Row.ZH_HANT=Copy(TEXT("返回選單"));
    Row.JA=Copy(TEXT("戻る"));Row.KO=Copy(TEXT("돌아가기"));
    Row.SourceText=Copy(TEXT("Старый исходник"));Row.Localizations.Add(TEXT("en"),Copy(TEXT("Old translation")));
    Row.AdditionalLanguages.Add(TEXT("en-GB"),Copy(TEXT("Return")));Table->AddRow(TEXT("Back"),Row);
    auto Text=[&](const TCHAR* Culture){return UMCUIDataLibrary::ResolveText(Table,TEXT("Back"),Copy(TEXT("Missing")),Culture).ToString();};
    TestEqual(TEXT("RU column wins over immutable alias"),Text(TEXT("ru")),FString(TEXT("Назад")));
    TestEqual(TEXT("EN column wins over old map"),Text(TEXT("en")),FString(TEXT("Back")));
    TestEqual(TEXT("Exact regional variant wins"),Text(TEXT("en-GB")),FString(TEXT("Return")));
    TestEqual(TEXT("Parent language column handles region"),Text(TEXT("fr-CA")),FString(TEXT("Retour")));
    TestEqual(TEXT("Empty optional language falls back to RU"),Text(TEXT("es")),FString(TEXT("Назад")));
    TestEqual(TEXT("Brazil has its own regional column"),Text(TEXT("pt-BR")),FString(TEXT("Voltar")));
    TestEqual(TEXT("Portugal does not implicitly use Brazilian copy"),Text(TEXT("pt-PT")),FString(TEXT("Назад")));
    TestEqual(TEXT("Simplified Chinese handles Steam region"),Text(TEXT("zh-CN")),FString(TEXT("返回")));
    TestEqual(TEXT("Traditional Chinese handles Steam region"),Text(TEXT("zh-TW")),FString(TEXT("返回選單")));
    TestEqual(TEXT("Traditional script handles Hong Kong"),Text(TEXT("zh-HK")),FString(TEXT("返回選單")));
    TestEqual(TEXT("Japanese regional fallback"),Text(TEXT("ja-JP")),FString(TEXT("戻る")));
    TestEqual(TEXT("Korean regional fallback"),Text(TEXT("ko-KR")),FString(TEXT("돌아가기")));
    Row.RU=Copy(TEXT("Вернуться"));Table->AddRow(TEXT("Back"),Row);
    TestEqual(TEXT("RU edits use the same stable key"),Text(TEXT("ru")),FString(TEXT("Вернуться")));
    TestEqual(TEXT("Native fallback uses edited RU"),Text(TEXT("de")),FString(TEXT("Вернуться")));
    TestEqual(TEXT("Legacy alias is unchanged"),Table->FindRow<FMCUILocalizationRow>(TEXT("Back"),TEXT("test"))->SourceText.ToString(),FString(TEXT("Старый исходник")));
    for(const TCHAR* Name:{TEXT("RU"),TEXT("EN"),TEXT("DE"),TEXT("FR"),TEXT("ES"),TEXT("PT_BR"),TEXT("ZH_HANS"),TEXT("ZH_HANT"),TEXT("JA"),TEXT("KO")})
    {
        const auto* Property=FindFProperty<FTextProperty>(FMCUILocalizationRow::StaticStruct(),Name);
        TestTrue(FString(Name)+TEXT(" is an editable text column"),Property && Property->HasAnyPropertyFlags(CPF_Edit));
    }
    for(const TCHAR* Name:{TEXT("SourceText"),TEXT("Localizations")})
    {
        const auto* Property=FMCUILocalizationRow::StaticStruct()->FindPropertyByName(Name);
        TestTrue(FString(Name)+TEXT(" remains serialized but hidden from the grid"),Property && !Property->HasAnyPropertyFlags(CPF_Transient|CPF_Edit) && Property->HasMetaData(TEXT("HideFromDataTableEditorColumn")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUICatalogPresentationTest,"MessControl.UI.Localization.SharedCatalogPreservesPresentationIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FMCUICatalogPresentationTest::RunTest(const FString& Parameters)
{
    UDataTable* Texts=Catalogue();FMCUILocalizationRow Name,Description;
    Name.RU=Copy(TEXT("Волна"));Name.EN=Copy(TEXT("Wave"));Description.RU=Copy(TEXT("Описание"));Description.EN=Copy(TEXT("Description"));
    Texts->AddRow(TEXT("UI.Card.Special.Title"),Name);Texts->AddRow(TEXT("UI.Card.Special.Description"),Description);
    auto* Cards=NewObject<UDataTable>();Cards->RowStruct=FMCUICardRow::StaticStruct();FMCUICardRow Card;
    Card.GameplayPerkID=TEXT("ActualGameplayID");Card.Image=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Gameplay/UI/Art/T_Test.T_Test")));
    Card.DisplayName=Copy(TEXT("Legacy name"));Card.Description=Copy(TEXT("Legacy description"));
    Card.DisplayNameKey=TEXT("UI.Card.Special.Title");Card.DescriptionKey=TEXT("UI.Card.Special.Description");Cards->AddRow(TEXT("ArtistChosenRow"),Card);
    const auto English=UMCUIDataLibrary::FindCardForGameplayID(Cards,TEXT("ActualGameplayID"),TEXT("en"),Texts);
    TestTrue(TEXT("Arbitrary presentation row still resolves by gameplay identity"),English.bFound);
    TestEqual(TEXT("Central title is selected"),English.DisplayName.ToString(),FString(TEXT("Wave")));
    TestEqual(TEXT("Central description is selected"),English.Description.ToString(),FString(TEXT("Description")));
    TestFalse(TEXT("Complete common language does not report legacy fallback"),English.bUsedFallback);
    TestEqual(TEXT("Authored image identity is preserved"),English.Image.ToSoftObjectPath().ToString(),Card.Image.ToSoftObjectPath().ToString());
    Name.RU=Copy(TEXT("Новая волна"));Texts->AddRow(Card.DisplayNameKey,Name);
    FDataTableRowHandle Handle;Handle.DataTable=Cards;Handle.RowName=TEXT("ArtistChosenRow");
    TestEqual(TEXT("Selected row handle uses edited central text"),UMCUIDataLibrary::ResolveCard(Handle,TEXT("ru"),Texts).DisplayName.ToString(),FString(TEXT("Новая волна")));
    Card.DisplayNameKey=TEXT("Missing.Title");Cards->AddRow(Handle.RowName,Card);
    const auto Missing=UMCUIDataLibrary::ResolveCard(Handle,TEXT("ru"),Texts);
    TestTrue(TEXT("Missing key retains a visible authored fallback"),Missing.bUsedFallback && Missing.DisplayName.ToString()==TEXT("Legacy name"));
    Card.DisplayNameKey=TEXT("UI.Card.Special.Title");Card.DescriptionKey=NAME_None;
    Card.Description=Copy(TEXT("Сила {Count}"));FMCUILocalizedCopy Legacy;
    Legacy.Description=Copy(TEXT("Power"));Card.Localizations.Add(TEXT("en"),Legacy);Cards->AddRow(Handle.RowName,Card);
    TArray<FString> Problems;
    TestFalse(TEXT("An unkeyed field still validates its active legacy translation"),UMCUIDataLibrary::ValidateContentTable(Cards,Problems,Texts));
    Card.Localizations[TEXT("en")].Description=Copy(TEXT("Power {Count}"));Cards->AddRow(Handle.RowName,Card);
    TestTrue(TEXT("A partially migrated custom row passes with matching arguments"),UMCUIDataLibrary::ValidateContentTable(Cards,Problems,Texts));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCUILegacyDataTest,"MessControl.UI.Localization.LegacyImportAndFormatValidation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
bool FMCUILegacyDataTest::RunTest(const FString& Parameters)
{
    UDataTable* Table=Catalogue();
    const auto ImportProblems=Table->CreateTableFromJSONString(TEXT("[{\"Name\":\"OldRow\",\"SourceText\":\"Старый текст\",\"Localizations\":{\"en\":\"Old English\"},\"Context\":\"Existing authored row\"}]"));
    TestTrue(TEXT("Legacy JSON imports without losing old field tags"),ImportProblems.IsEmpty());
    TestEqual(TEXT("Legacy English remains readable before migration"),UMCUIDataLibrary::ResolveText(Table,TEXT("OldRow"),FText::GetEmpty(),TEXT("en")).ToString(),FString(TEXT("Old English")));
    TestEqual(TEXT("Legacy native remains readable before migration"),UMCUIDataLibrary::ResolveText(Table,TEXT("OldRow"),FText::GetEmpty(),TEXT("ru")).ToString(),FString(TEXT("Старый текст")));
    Table->EmptyTable();FMCUILocalizationRow Pattern;Pattern.RU=Copy(TEXT("Осталось {Count}"));Pattern.EN=Copy(TEXT("Remaining"));Table->AddRow(TEXT("Count"),Pattern);TArray<FString> Problems;
    TestFalse(TEXT("Missing format argument is rejected"),UMCUIDataLibrary::ValidateContentTable(Table,Problems));
    Pattern.EN=Copy(TEXT("Remaining {Count}"));Table->AddRow(TEXT("Count"),Pattern);
    TestTrue(TEXT("Valid placeholder pattern passes with empty optional languages"),UMCUIDataLibrary::ValidateContentTable(Table,Problems));
    return true;
}
#endif
