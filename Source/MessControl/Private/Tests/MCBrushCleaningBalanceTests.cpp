#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCCoffeeWipe.h"
#include "MCSurfaceWipe.h"
#include "MCInventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include <limits>

namespace MCBrushCleaningBalanceTests
{
struct FDuration {float Seconds=0;int32 Steps=0;};

FDuration PuddleContact(float Dt,float ToolRate)
{
    TArray<uint8> Mask;TArray<float> Fractional;FMCCoffeeWipe::Reset(Mask);
    // One unchanged local trajectory through an actual mask cell. Do not
    // substitute a global completion timer or change the footprint for speed.
    const FVector2D At(32.5/64.,32.5/64.);const int32 Probe=32*64+32;
    FDuration Result;
    while(Mask[Probe]>63 && Result.Seconds<2.f) {
        FMCCoffeeWipe::Stroke(Mask,At,At,.12f,Dt,&Fractional,ToolRate);
        Result.Seconds+=Dt;++Result.Steps;
    }
    return Result;
}

FDuration ToothContact(float Dt,float ToolRate)
{
    TArray<uint8> Mask;TArray<float> Precise;FMCSurfaceWipe::Reset(Mask);
    const FVector At=FVector(8,8,8)/15.;const int32 Probe=FMCSurfaceWipe::Index(8,8,8);
    FDuration Result;
    while(Mask[Probe]>63 && Result.Seconds<2.f) {
        // Arena brushing retains its existing two-exposure stroke convention.
        FMCSurfaceWipe::Stroke(Mask,At,At,FVector(200),45.f,Dt*2,&Precise,ToolRate);
        Result.Seconds+=Dt;++Result.Steps;
    }
    return Result;
}

// The previous behaviour is a reference exposure, not the new balance constant.
// Stop at the same visible working-residue threshold as the real mask solvers.
FDuration LegacyContact(float Dt,float ToolRate,bool Tooth)
{
    float Coverage=255;FDuration Result;
    while(FMath::FloorToInt(Coverage)>63 && Result.Seconds<2.f) {
        if(Tooth) Coverage=FMath::Max(0.f,Coverage-950.f*FMath::Min(2*Dt,.1f)*ToolRate);
        else Coverage*=FMath::Exp(-12.f*FMath::Min(Dt,.1f)*ToolRate);
        Result.Seconds+=Dt;++Result.Steps;
    }
    return Result;
}

struct FWholeStainComparison
{
    int32 VisibleCells=0;
    float PreviousSeconds=0,CurrentSeconds=0;
    float PreviousRemaining=1,CurrentRemaining=1;
    bool bPreviousComplete=false,bCurrentComplete=false;
};

FWholeStainComparison CompareWholeOrdinaryStain(float HalfSize,int32 Seed)
{
    // Author one real seeded outline and one WORLD-space traversal, before
    // either solver runs. Width cannot change the route, speed, dose or clock.
    // This is a solver comparison, not a substitute for pawn reach/occlusion.
    constexpr float PreviousRadiusCm=36.f,RowSpacingCm=48.f,TravelCmPerSecond=180.f;
    constexpr float Dt=1.f/30,MaximumSeconds=120.f;
    const TArray<uint8> Full;
    TArray<int32> Visible;
    FVector2D Min(HalfSize),Max(-HalfSize);
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for(int32 X=0;X<FMCCoffeeWipe::Size;++X) {
        const FVector2D UV((X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size);
        if(!FMCCoffeeWipe::WetAt(Full,UV,Seed,false,100)) continue;
        Visible.Add(Y*FMCCoffeeWipe::Size+X);
        const FVector2D World=(UV-FVector2D(.5))*(2*HalfSize);
        Min.X=FMath::Min(Min.X,World.X);Min.Y=FMath::Min(Min.Y,World.Y);
        Max.X=FMath::Max(Max.X,World.X);Max.Y=FMath::Max(Max.Y,World.Y);
    }
    FWholeStainComparison Result;Result.VisibleCells=Visible.Num();
    if(Visible.IsEmpty()) return Result;
    TArray<FVector2D> Route;
    const int32 Rows=FMath::Max(1,FMath::CeilToInt((Max.Y-Min.Y)/RowSpacingCm));
    for(int32 Row=0;Row<=Rows;++Row) {
        const float Y=FMath::Lerp(Min.Y,Max.Y,double(Row)/Rows);
        Route.Add(FVector2D(Row%2?Max.X:Min.X,Y));
        Route.Add(FVector2D(Row%2?Min.X:Max.X,Y));
    }
    auto Remaining=[&](const TArray<uint8>& Mask) {
        // Exact ordinary RemainingLiquid weighting and its real <.025 finish
        // threshold. Bytes outside the seeded visible stain never count.
        float Left=0;
        for(int32 Index:Visible) Left+=FMath::Clamp((Mask[Index]/255.f-.25f)/.75f,0.f,1.f);
        return Left/Visible.Num();
    };
    TArray<uint8> Previous,Current;TArray<float> PreviousFractional,CurrentFractional;
    FMCCoffeeWipe::Reset(Previous);FMCCoffeeWipe::Reset(Current);
    FVector2D Point=Route[0];int32 Next=1;
    for(int32 Step=0;Step<FMath::CeilToInt(MaximumSeconds/Dt);++Step) {
        const FVector2D From=Point;
        float Distance=TravelCmPerSecond*Dt;
        // Include corner/return travel in the clock; never erase a teleported
        // bridge or skip the remaining route just because a sector looks dry.
        while(Distance>KINDA_SMALL_NUMBER) {
            const FVector2D Delta=Route[Next]-Point;const float Length=Delta.Size();
            if(Length<=Distance) {Point=Route[Next];Distance-=Length;Next=(Next+1)%Route.Num();}
            else {Point+=Delta*(Distance/Length);Distance=0;}
        }
        const FVector2D FromUV=From/(2*HalfSize)+FVector2D(.5),ToUV=Point/(2*HalfSize)+FVector2D(.5);
        FMCCoffeeWipe::Stroke(Previous,FromUV,ToUV,PreviousRadiusCm/(2*HalfSize),Dt,&PreviousFractional,1.f);
        FMCCoffeeWipe::Stroke(Current,FromUV,ToUV,MCBrushCleaningBalance::OrdinaryBrushRadiusCm/(2*HalfSize),Dt,&CurrentFractional,1.f);
        Result.PreviousRemaining=Remaining(Previous);Result.CurrentRemaining=Remaining(Current);
        const float Time=(Step+1)*Dt;
        if(!Result.bCurrentComplete && Result.CurrentRemaining<.025f) {Result.bCurrentComplete=true;Result.CurrentSeconds=Time;}
        if(!Result.bPreviousComplete && Result.PreviousRemaining<.025f) {Result.bPreviousComplete=true;Result.PreviousSeconds=Time;}
        if(Result.bPreviousComplete && Result.bCurrentComplete) break;
    }
    return Result;
}

struct FNearestStainResult {float Seconds=0,Remaining=1;bool bComplete=false;};
FNearestStainResult FollowNearestWetPolicy(float HalfSize,int32 Seed,float RadiusCm)
{
    const TArray<uint8> Full;
    TArray<int32> Visible;
    for(int32 Y=0;Y<FMCCoffeeWipe::Size;++Y) for(int32 X=0;X<FMCCoffeeWipe::Size;++X)
        if(FMCCoffeeWipe::WetAt(Full,{(X+.5)/FMCCoffeeWipe::Size,(Y+.5)/FMCCoffeeWipe::Size},Seed,false,100)) Visible.Add(Y*FMCCoffeeWipe::Size+X);
    FNearestStainResult Result;if(Visible.IsEmpty()) return Result;
    TArray<uint8> Mask;TArray<float> Fractional;FMCCoffeeWipe::Reset(Mask);
    FVector2D Center=FVector2D::ZeroVector,PreviousPoint=Center;
    constexpr float Dt=1.f/60;
    // Same nearest-wet policy, center speed and lateral stroke for both widths.
    // This flat solver fixture intentionally makes no pawn-reach/facing claim.
    for(int32 Step=0;Step<3600;++Step) {
        int32 Best=INDEX_NONE;double Score=TNumericLimits<double>::Max();
        const auto Scan=[&](bool Coarse) {
            for(int32 Index:Visible) {
                const int32 X=Index%FMCCoffeeWipe::Size,Y=Index/FMCCoffeeWipe::Size;
                if(Mask[Index]<=63 || (Coarse && (!(X&1) || !(Y&1)))) continue;
                const FVector2D Candidate=((FVector2D(X+.5,Y+.5)/FMCCoffeeWipe::Size)-FVector2D(.5))*(2*HalfSize);
                const double Distance=(Candidate-PreviousPoint).SizeSquared();
                if(Distance<Score) {Score=Distance;Best=Index;}
            }
        };
        Scan(true);if(Best==INDEX_NONE) Scan(false);
        if(Best==INDEX_NONE) break;
        const FVector2D Target=((FVector2D(Best%FMCCoffeeWipe::Size+.5,Best/FMCCoffeeWipe::Size+.5)/FMCCoffeeWipe::Size)-FVector2D(.5))*(2*HalfSize);
        Center+=(Target-Center).GetClampedToMaxSize(180.f*Dt);
        const FVector2D Point=Center+FVector2D(0,FMath::Sin((Step+1)*Dt*22)*6.f);
        // Preserve the ordinary one-third-second approach. Time spent here is
        // charged to both widths instead of assuming instantaneous first work.
        if(Step<20) {PreviousPoint=Point;continue;}
        const FVector2D From=Step>0 && (Point-PreviousPoint).SizeSquared()<=FMath::Square(40.f)?PreviousPoint:Point;
        FMCCoffeeWipe::Stroke(Mask,From/(2*HalfSize)+FVector2D(.5),Point/(2*HalfSize)+FVector2D(.5),RadiusCm/(2*HalfSize),Dt,&Fractional,1.f);
        PreviousPoint=Point;Result.Seconds=(Step+1)*Dt;
        float Left=0;for(int32 Index:Visible) Left+=FMath::Clamp((Mask[Index]/255.f-.25f)/.75f,0.f,1.f);
        Result.Remaining=Left/Visible.Num();
        if(Result.Remaining<.025f) {Result.bComplete=true;break;}
    }
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrushContactDurationTest,"MessControl.Cleanup.BrushBalance.SamePathNeedsLessContactTime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrushContactDurationTest::RunTest(const FString&)
{
    using namespace MCBrushCleaningBalanceTests;
    for(float Dt:{1.f/30,1.f/60,1.f/120}) for(float ToolRate:{1.f,2.f}) for(bool Tooth:{false,true}) {
        const FDuration Previous=LegacyContact(Dt,ToolRate,Tooth);
        const FDuration Current=Tooth?ToothContact(Dt,ToolRate):PuddleContact(Dt,ToolRate);
        const FString Label=FString::Printf(TEXT("%s toolRate=%.0f FPS=%.0f"),Tooth?TEXT("Tooth"):TEXT("Puddle"),ToolRate,1/Dt);
        AddInfo(FString::Printf(TEXT("%s previous=%.6fs/%d contacts current=%.6fs/%d contacts"),
            *Label,Previous.Seconds,Previous.Steps,Current.Seconds,Current.Steps));
        TestTrue(*(Label+TEXT(" reaches the actual dry threshold")),Current.Seconds<2.f);
        // Discrete accepted contacts may round both versions to the same final
        // frame. Permit one frame of quantisation, not a widened gameplay goal.
        TestTrue(*(Label+TEXT(" reaches about 60 percent of former contact duration")),
            FMath::Abs(Current.Seconds-.6f*Previous.Seconds)<=Dt*1.01f);
        TestTrue(*(Label+TEXT(" never requires more accepted contacts")),Current.Steps<=Previous.Steps);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrushLocalityAndFractionalTest,"MessControl.Cleanup.BrushBalance.LocalityFrameRateAndToolCap",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrushLocalityAndFractionalTest::RunTest(const FString&)
{
    const FVector At=FVector(8,8,0)/15.;const int32 ToothProbe=FMCSurfaceWipe::Index(8,8,0);
    const FVector2D UV(32.5/64.,32.5/64.);const int32 PuddleProbe=32*64+32;
    TArray<uint8> Tooth30,Tooth120,Floor30,Floor120;
    for(int32 FPS:{30,120}) {
        TArray<uint8> Tooth,Floor;TArray<float> TP,FP;
        FMCSurfaceWipe::Reset(Tooth);FMCCoffeeWipe::Reset(Floor);
        // Equal short exposures stay below saturation so fractional accuracy
        // and boost ratio remain observable, rather than every cell becoming 0.
        for(int32 Step=0;Step<FPS/10;++Step) {
            FMCSurfaceWipe::Stroke(Tooth,At,At,FVector(200),45.f,1.f/FPS,&TP);
            FMCCoffeeWipe::Stroke(Floor,UV,UV,.12f,1.f/FPS,&FP);
        }
        TestTrue(TEXT("Accepted exposure actually cleans the contacted enamel"),Tooth[ToothProbe]<255);
        TestTrue(TEXT("Accepted exposure actually cleans the contacted floor"),Floor[PuddleProbe]<255);
        TestEqual(TEXT("A stronger dose cannot jump to the opposite tooth face"),Tooth[FMCSurfaceWipe::Index(8,8,15)],uint8(255));
        TestEqual(TEXT("A stronger dose does not broaden the floor footprint"),Floor[2*64+2],uint8(255));
        const auto Before=Floor;
        TestFalse(TEXT("Out-of-area brush requests still do no work"),FMCCoffeeWipe::Stroke(Floor,{-2,-2},{-1,-1},.12f,.1f,&FP));
        TestTrue(TEXT("Rejected requests preserve existing work"),Floor==Before);
        if(FPS==30) {Tooth30=MoveTemp(Tooth);Floor30=MoveTemp(Floor);}
        else {Tooth120=MoveTemp(Tooth);Floor120=MoveTemp(Floor);}
    }
    TestTrue(TEXT("Actual tooth dose is stable across 30 and 120 FPS"),FMath::Abs(int32(Tooth30[ToothProbe])-int32(Tooth120[ToothProbe]))<=1);
    TestTrue(TEXT("Actual floor dose is stable across 30 and 120 FPS"),FMath::Abs(int32(Floor30[PuddleProbe])-int32(Floor120[PuddleProbe]))<=1);
    TArray<uint8> Normal,Fast,Capped;TArray<float> NP,FP,CP;
    for(auto* Mask:{&Normal,&Fast,&Capped}) FMCSurfaceWipe::Reset(*Mask);
    FMCSurfaceWipe::Stroke(Normal,At,At,FVector(200),45.f,.01f,&NP,1.f);
    FMCSurfaceWipe::Stroke(Fast,At,At,FVector(200),45.f,.01f,&FP,2.f);
    FMCSurfaceWipe::Stroke(Capped,At,At,FVector(200),45.f,.01f,&CP,200.f);
    TestTrue(TEXT("The ordinary tool upgrade still doubles unsaturated accepted work"),FMath::IsNearlyEqual(255-FP[ToothProbe],2*(255-NP[ToothProbe]),.001f));
    TestTrue(TEXT("The existing tool-rate cap survives the baseline dose change"),Fast==Capped);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrushWholeVisibleStainTest,"MessControl.Cleanup.BrushBalance.WholeVisibleStainWidthComparison",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrushWholeVisibleStainTest::RunTest(const FString&)
{
    for(FIntPoint Case:{FIntPoint(180,3),FIntPoint(240,17),FIntPoint(440,31)}) {
        const float HalfSize=Case.X;const int32 Seed=Case.Y;
        const auto Result=MCBrushCleaningBalanceTests::CompareWholeOrdinaryStain(HalfSize,Seed);
        const FString Label=FString::Printf(TEXT("halfSize=%.0fcm seed=%d"),HalfSize,Seed);
        TestTrue(*(Label+TEXT(" has real authored visible cells")),Result.VisibleCells>0);
        TestTrue(*(Label+TEXT(" previous-width solver fully completes the visible stain")),Result.bPreviousComplete && Result.PreviousRemaining<.025f);
        TestTrue(*(Label+TEXT(" current-width solver fully completes the visible stain")),Result.bCurrentComplete && Result.CurrentRemaining<.025f);
        if(Result.bPreviousComplete && Result.bCurrentComplete) {
            const float Ratio=Result.CurrentSeconds/Result.PreviousSeconds;
            AddInfo(FString::Printf(TEXT("Whole ordinary stain %s cells=%d samePathSpeed=180cm/s sameDt=1/30s sameDose=5/3 previous36=%.3fs current%.1f=%.3fs timeRatio=%.4f shorter=%.1f%% desiredRatio=0.6"),
                *Label,Result.VisibleCells,Result.PreviousSeconds,MCBrushCleaningBalance::OrdinaryBrushRadiusCm,Result.CurrentSeconds,Ratio,(1-Ratio)*100));
            // A fixed complete route can still visit its last edge late. Expose
            // the actual ratio above; do not label any local-dose PASS as proof
            // of a forty-percent reduction in whole-stain gameplay duration.
            TestTrue(*(Label+TEXT(" wider footprint completes the whole stain earlier")),Result.CurrentSeconds<Result.PreviousSeconds);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrushNearestWetStainTest,"MessControl.Cleanup.BrushBalance.NearestWetWholeStainWidthComparison",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrushNearestWetStainTest::RunTest(const FString&)
{
    for(FIntPoint Case:{FIntPoint(180,3),FIntPoint(240,17),FIntPoint(440,31)}) {
        const float HalfSize=Case.X;const int32 Seed=Case.Y;
        const auto Previous=MCBrushCleaningBalanceTests::FollowNearestWetPolicy(HalfSize,Seed,36.f);
        const auto Current=MCBrushCleaningBalanceTests::FollowNearestWetPolicy(HalfSize,Seed,MCBrushCleaningBalance::OrdinaryBrushRadiusCm);
        const FString Label=FString::Printf(TEXT("nearest-wet halfSize=%.0fcm seed=%d"),HalfSize,Seed);
        TestTrue(*(Label+TEXT(" baseline completes the actual visible residue")),Previous.bComplete && Previous.Remaining<.025f);
        TestTrue(*(Label+TEXT(" widened brush completes the actual visible residue")),Current.bComplete && Current.Remaining<.025f);
        if(Previous.bComplete && Current.bComplete) {
            const float Ratio=Current.Seconds/Previous.Seconds;
            AddInfo(FString::Printf(TEXT("Whole ordinary stain %s samePolicy sameSpeed180 sameDt1/60 sameDose5/3 sameApproach1/3 previous36=%.3fs current%.1f=%.3fs ratio=%.4f shorter=%.1f%% desiredRatio=0.6"),
                *Label,Previous.Seconds,MCBrushCleaningBalance::OrdinaryBrushRadiusCm,Current.Seconds,Ratio,(1-Ratio)*100));
            TestTrue(*(Label+TEXT(" wider brush reduces complete-stain work")),Current.Seconds<Previous.Seconds);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrushRuntimeWidthTest,"MessControl.Cleanup.BrushBalance.RuntimeWidthAddsLocalStripeWithoutMoreDose",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrushRuntimeWidthTest::RunTest(const FString&)
{
    auto* Inventory=NewObject<UMCInventoryComponent>();
    TestEqual(TEXT("The actual default inventory uses the shared radius"),Inventory->CleaningRadius(240.f),MCBrushCleaningBalance::OrdinaryBrushRadiusCm);
    TestEqual(TEXT("A large stain does not silently become a whole-stain ordinary tool"),Inventory->CleaningRadius(440.f),Inventory->CleaningRadius(180.f));
    TestEqual(TEXT("Wider baseline adds no tool-rate bonus"),Inventory->CleaningSpeedMultiplier(),1.f);
    TArray<uint8> Previous,Current;TArray<float> PreviousFractional,CurrentFractional;
    FMCCoffeeWipe::Reset(Previous);FMCCoffeeWipe::Reset(Current);
    const FVector2D At(32.5/64.,32.5/64.);constexpr float FullWidthCm=480.f;
    FMCCoffeeWipe::Stroke(Previous,At,At,36.f/FullWidthCm,.01f,&PreviousFractional);
    FMCCoffeeWipe::Stroke(Current,At,At,Inventory->CleaningRadius(240.f)/FullWidthCm,.01f,&CurrentFractional);
    TestEqual(TEXT("At equal accepted time the center receives the same dose"),Current[32*64+32],Previous[32*64+32]);
    TestEqual(TEXT("The previous brush misses a point45cm to the side"),Previous[32*64+38],uint8(255));
    TestTrue(TEXT("The new actual brush stripe cleans that nearby point"),Current[32*64+38]<255);
    TestEqual(TEXT("A point67.5cm away remains untouched"),Current[32*64+41],uint8(255));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCBrushProfileWidthTest,"MessControl.Cleanup.BrushBalance.AuthoredProfileControlsRuntimeStripe",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCBrushProfileWidthTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Owner=World->SpawnActor<AActor>();
    auto* Profile=NewObject<UMCEquipmentProfile>(Owner);
    TestEqual(TEXT("New equipment profiles default to the shared radius"),Profile->BrushCleaningRadius,MCBrushCleaningBalance::OrdinaryBrushRadiusCm);
    Profile->BrushCleaningRadius=36.f;
    auto* Inventory=NewObject<UMCInventoryComponent>(Owner);Inventory->Profile=Profile;
    Owner->AddInstanceComponent(Inventory);Inventory->RegisterComponent();
    TestTrue(TEXT("The profile fixture component begins normally"),Inventory->HasBegunPlay());
    TestEqual(TEXT("An authored36cm profile reaches the actual runtime path"),Inventory->CleaningRadius(240.f),36.f);
    Profile->BrushCleaningRadius=82.f;
    TestEqual(TEXT("Runtime uses the authored profile, not only a hardcoded constant"),Inventory->CleaningRadius(240.f),82.f);
    Profile->BrushCleaningRadius=999.f;
    TestEqual(TEXT("A profile cannot author a remote cleanup radius"),Inventory->CleaningRadius(440.f),120.f);
    Profile->BrushCleaningRadius=std::numeric_limits<float>::quiet_NaN();
    TestEqual(TEXT("Nonfinite profile data falls back safely"),Inventory->CleaningRadius(240.f),MCBrushCleaningBalance::OrdinaryBrushRadiusCm);
    World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    return true;
}
#endif
