#if WITH_DEV_AUTOMATION_TESTS
#include "MCFoodCollectionComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCManualThrowRangeTest,"MessControl.Food.ManualThrow.HalfBallisticRange",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCManualThrowRangeTest::RunTest(const FString&)
{
    const auto* Collection=NewObject<UMCFoodCollectionComponent>();
    const double OldSpeed=1800.,NewSpeed=Collection->EffectiveThrowSpeed(),Gravity=980.;
    // Physical flight to a plane: elevated stack releases take slightly longer
    // than same-height throws. Test actual horizontal reach, not velocity ratio.
    auto Reach=[Gravity](double Speed,double Height) {
        const double Horizontal=Speed/FMath::Sqrt(2.);
        const double Time=(Horizontal+FMath::Sqrt(Horizontal*Horizontal+2.*Gravity*Height))/Gravity;
        return Horizontal*Time;
    };
    TestTrue(TEXT("A standard same-height 45-degree throw reaches half its old distance"),
        FMath::IsNearlyEqual(Reach(NewSpeed,0.)/Reach(OldSpeed,0.),.5,.001));
    for(double Height:{100.,200.,300.}) {
        const double Ratio=Reach(NewSpeed,Height)/Reach(OldSpeed,Height);
        TestTrue(TEXT("A raised hand or stack still lands at approximately half the old reach"),Ratio>=.5 && Ratio<.55);
    }
    return true;
}
#endif
