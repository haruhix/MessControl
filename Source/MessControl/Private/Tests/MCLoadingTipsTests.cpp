#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "MCLoadingTips.h"
#include "MCUIDataTypes.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCLoadingTipTimingTest,"MessControl.Loading.Tips.CrossfadeTiming",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCLoadingTipTimingTest::RunTest(const FString&)
{
    const int32 Count=MCLoadingTips::Count();
    const auto Begin=MCLoadingTips::Evaluate(0.,Count);
    TestEqual(TEXT("First entry starts readable"),Begin.Current,0);
    TestEqual(TEXT("No incoming text before the hold"),MCLoadingTips::Evaluate(4.99,Count).NextOpacity,0.f);
    const auto Middle=MCLoadingTips::Evaluate(5.1,Count);
    TestTrue(TEXT("The 0.2-second transition overlaps both text layers"),FMath::IsNearlyEqual(Middle.CurrentOpacity,.5f,.001f) && FMath::IsNearlyEqual(Middle.NextOpacity,.5f,.001f));
    const auto Next=MCLoadingTips::Evaluate(5.2,Count);
    TestEqual(TEXT("Next row follows one five-second hold and fade"),Next.Current,1);
    TestEqual(TEXT("A new row starts fully visible"),Next.CurrentOpacity,1.f);
    TestEqual(TEXT("The sequence wraps"),MCLoadingTips::Evaluate(5.2*Count,Count).Current,0);
    for(double Time:{-1.,0.,5.,5.1,5.2,999999.}) {
        const auto Frame=MCLoadingTips::Evaluate(Time,Count);
        TestTrue(TEXT("Every frame retains valid neighbours and complementary opacity"),Frame.Current>=0 && Frame.Current<Count && Frame.Next>=0 && Frame.Next<Count && FMath::IsNearlyEqual(Frame.CurrentOpacity+Frame.NextOpacity,1.f));
    }
    TestEqual(TEXT("Nonfinite clock starts safely"),MCLoadingTips::Evaluate(std::numeric_limits<double>::quiet_NaN(),Count).Current,0);
    TestEqual(TEXT("No tips produce no index"),MCLoadingTips::Evaluate(1.,0).Current,INDEX_NONE);
    TestEqual(TEXT("One tip does not fade into itself"),MCLoadingTips::Evaluate(5.1,1).NextOpacity,0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCLoadingTipLocalizationTest,"MessControl.Loading.Tips.LocalizedSnapshotAndFallback",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCLoadingTipLocalizationTest::RunTest(const FString&)
{
    const auto RU=MCLoadingTips::Resolve(nullptr,TEXT("ru"));
    const auto EN=MCLoadingTips::Resolve(nullptr,TEXT("en"));
    TestEqual(TEXT("Eight short tips are available without a localization asset"),EN.Num(),8);
    TestEqual(TEXT("Both cultures have the same deck"),RU.Num(),EN.Num());
    for(int32 Index=0;Index<EN.Num();++Index)
        TestTrue(TEXT("Missing rows have readable culture-specific fallbacks"),!RU[Index].IsEmpty() && !EN[Index].IsEmpty() && !RU[Index].EqualTo(EN[Index]));
    auto* Table=NewObject<UDataTable>();Table->RowStruct=FMCUILocalizationRow::StaticStruct();
    FMCUILocalizationRow Row;Row.RU=FText::FromString(TEXT("Изменённый совет"));Row.EN=FText::FromString(TEXT("Edited tip"));
    Table->AddRow(MCLoadingTips::Key(0),Row);
    const auto Snapshot=MCLoadingTips::Resolve(Table,TEXT("en"));
    TestEqual(TEXT("Existing project table overrides native copy"),Snapshot[0].ToString(),FString(TEXT("Edited tip")));
    Table->RemoveRow(MCLoadingTips::Key(0));
    TestEqual(TEXT("Prepared Slate snapshot survives later table changes"),Snapshot[0].ToString(),FString(TEXT("Edited tip")));
    TestFalse(TEXT("Heading also has a no-asset fallback"),MCLoadingTips::Heading(nullptr,TEXT("en")).IsEmpty());
    return true;
}
#endif
