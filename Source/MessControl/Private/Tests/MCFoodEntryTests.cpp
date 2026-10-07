#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCFoodEntrySettings.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodEntryTrajectoryTest,"MessControl.Food.Entry.BallisticTrajectory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodEntryTrajectoryTest::RunTest(const FString&)
{
    const FMCFoodEntrySettings Settings;
    const FBox Tongue(FVector(-1000,-750,-100),FVector(1100,650,80));
    // Landing bands can choose both sides of the tongue, but every piece starts
    // in the central front opening so the player can read its incoming motion.
    for(const FVector Landing:{FVector(-500,-400,125),FVector(50,350,200),FVector(700,0,-50)})
        for(const float GravityZ:{-980.f,-490.f,-1600.f})
        {
            FVector Start,Velocity;
            if(!TestTrue(TEXT("Valid tongue and gravity produce a launch"),Settings.BuildTrajectory(Tongue,Landing,GravityZ,Start,Velocity))) return false;
            TestTrue(TEXT("Food enters from outside the front of the tongue"),Start.X<Tongue.Min.X && Velocity.X>0);
            TestEqual(TEXT("Entry stays in the center of the visible opening"),Start.Y,Tongue.GetCenter().Y,.001);
            TestEqual(TEXT("Entry stays above the highest tongue point even for a low landing"),Start.Z,FMath::Max(Landing.Z,Tongue.Max.Z)+Settings.EntryHeight,.001);
            const double T=Settings.FlightSeconds;
            const FVector End=Start+Velocity*T+FVector(0,0,.5*GravityZ*T*T);
            TestTrue(TEXT("Ballistic flight reaches the selected landing center"),End.Equals(Landing,.001));
            const double ApexTime=-Velocity.Z/GravityZ;
            TestTrue(TEXT("The readable arc rises before descending"),ApexTime>0 && ApexTime<T);
            const FVector Apex=Start+Velocity*ApexTime+FVector(0,0,.5*GravityZ*ApexTime*ApexTime);
            TestTrue(TEXT("The apex is above the opening and landing"),Apex.Z>Start.Z && Apex.Z>Landing.Z);
            TestTrue(TEXT("Arrival has downward motion"),Velocity.Z+GravityZ*T<0);
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCFoodEntrySanitizeTest,"MessControl.Food.Entry.SafeLimitsAndInvalidGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCFoodEntrySanitizeTest::RunTest(const FString&)
{
    const float NaN=std::numeric_limits<float>::quiet_NaN();
    for(const float Value:{NaN,std::numeric_limits<float>::infinity(),-1000.f,100000.f})
    {
        FMCFoodEntrySettings Settings;
        Settings.FlightSeconds=Settings.EntryHeight=Settings.OutsideDistance=Settings.PushSpeed=Value;
        Settings.Sanitize();
        TestTrue(TEXT("Flight time has a finite supported range"),FMath::IsFinite(Settings.FlightSeconds) && Settings.FlightSeconds>=.75f && Settings.FlightSeconds<=3.f);
        TestTrue(TEXT("Entry height stays readable and finite"),FMath::IsFinite(Settings.EntryHeight) && Settings.EntryHeight>=150.f && Settings.EntryHeight<=900.f);
        TestTrue(TEXT("The front offset stays finite and outside the tongue"),FMath::IsFinite(Settings.OutsideDistance) && Settings.OutsideDistance>=0 && Settings.OutsideDistance<=600.f);
        TestTrue(TEXT("Entry push is a bounded gentle nudge"),FMath::IsFinite(Settings.PushSpeed) && Settings.PushSpeed>=0 && Settings.PushSpeed<=180.f);
        const FMCFoodEntrySettings Once=Settings;
        Settings.Sanitize();
        TestEqual(TEXT("Sanitizing flight time twice is stable"),Settings.FlightSeconds,Once.FlightSeconds);
        TestEqual(TEXT("Sanitizing entry height twice is stable"),Settings.EntryHeight,Once.EntryHeight);
        TestEqual(TEXT("Sanitizing entry offset twice is stable"),Settings.OutsideDistance,Once.OutsideDistance);
        TestEqual(TEXT("Sanitizing push twice is stable"),Settings.PushSpeed,Once.PushSpeed);
    }
    const FMCFoodEntrySettings Settings;
    const FBox Tongue(FVector(-1000,-750,-100),FVector(1100,650,80));
    for(const float Gravity:{NaN,0.f,980.f})
    {
        FVector Start(100),Velocity(100);
        TestFalse(TEXT("Invalid gravity is rejected"),Settings.BuildTrajectory(Tongue,FVector(0,0,125),Gravity,Start,Velocity));
        TestTrue(TEXT("A rejected trajectory clears output vectors"),Start.IsZero() && Velocity.IsZero());
    }
    FVector Start,Velocity;
    TestFalse(TEXT("Uninitialized tongue bounds are rejected"),Settings.BuildTrajectory(FBox(ForceInit),FVector(0,0,125),-980,Start,Velocity));
    TestFalse(TEXT("A tongue with no front-to-back extent is rejected"),Settings.BuildTrajectory(FBox(FVector(0,-500,0),FVector(0,500,10)),FVector(0,0,125),-980,Start,Velocity));
    TestFalse(TEXT("A nonfinite landing point is rejected"),Settings.BuildTrajectory(Tongue,FVector(NaN,0,125),-980,Start,Velocity));
    return true;
}
#endif
