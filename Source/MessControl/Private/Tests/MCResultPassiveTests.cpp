#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCResultPassives.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCMouthSurface.h"
#include "MCArenaTooth.h"
#include "MCFoodActor.h"
#include "MCCoffeeWipe.h"
#include "MCSurfaceWipe.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include <limits>

namespace MCResultPassiveTests
{
struct FPlayer
{
    AMCPlayerController* Controller=nullptr;
    AMCPlayerState* State=nullptr;
    AMCToothCharacter* Hero=nullptr;
    UMCPerkComponent* Perks=nullptr;
};
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameState* State=nullptr;
    UDataTable* Table=nullptr;
    UMCResultPassives* Results=nullptr;
    FPlayer Player;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/MessControl.MCGameMode"));World->SetGameMode(URL);
        auto* Mode=World->GetAuthGameMode<AMCGameMode>();
        Mode->bUseSingleDayLoop=false;Mode->bUseDayOnePlan=false;Mode->bUseAdaptiveDirector=false;
        World->InitializeActorsForPlay(URL);World->BeginPlay();Mode->SetActorTickEnabled(false);
        State=World->GetGameState<AMCGameState>();State->Phase=EMCShiftPhase::Working;
        State->bLobbyWaiting=false;State->bTutorialActive=false;State->bPhysicalBrushes=false;
        Table=NewObject<UDataTable>(World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
        for(const TCHAR* ID:{TEXT("B2_ReturnTrip"),TEXT("B2_CleanBreath"),TEXT("B2_NeatFreak"),
            TEXT("B2_LastStroke"),TEXT("B2_CleanAcceleration"),TEXT("B2_RefreshingWork")}) {
            FMCPerkDefinition Row;Row.Kind=EMCPerkKind::Passive;Row.bRoguePerk=true;Row.MaxStacks=1;
            Table->AddRow(FName(ID),Row);
        }
        Player=AddPlayer();Results=UMCResultPassives::ForWorld(World);
    }
    ~FWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    FPlayer AddPlayer(float MaxHealth=100)
    {
        FPlayer P;P.Controller=World->SpawnActor<AMCPlayerController>();
        P.State=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity,P.Controller);
        P.State->Perks->PerkTable=Table;P.State->FinishSpawning(FTransform::Identity);
        P.Controller->SetPlayerState(P.State);P.State->SetOwner(P.Controller);State->AddPlayerState(P.State);
        P.Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10000),FRotator::ZeroRotator);
        P.Controller->Possess(P.Hero);P.Controller->bShowMouseCursor=false;
        P.Hero->GetCharacterMovement()->DisableMovement();P.Hero->SetActorTickEnabled(false);
        P.Hero->Status->Initialize(MaxHealth);P.Perks=P.State->Perks;
        return P;
    }
    void Health(FPlayer& P,float Value)
    {auto Next=P.Hero->Status->State;Next.Health=Value;P.Hero->Status->Restore(Next);}
    void Parameter(FName ID,FName Key,float Value)
    {Table->FindRow<FMCPerkDefinition>(ID,TEXT("Result fixture"))->Parameters.Add(Key,Value);}
    AMCMouthSurface* Surface(FVector Position=FVector(0,0,10000),uint8 Remaining=255)
    {
        const FTransform Pose(Position);
        auto* Patch=World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
        Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=100;Patch->FinishSpawning(Pose);
        Redirty(Patch,Remaining);return Patch;
    }
    void Redirty(AMCMouthSurface* Patch,uint8 Remaining=255)
    {
        Patch->Status->ApplyCoffee(1);
        TArray<uint8> Deposit;Deposit.Init(0,FMCCoffeeWipe::Count);Deposit[32*FMCCoffeeWipe::Size+32]=255;
        Patch->SetLiquidDepositMask(Deposit);
        // This intentionally small, explicit mask fixture uses the real native
        // removal and layer completion path, independent of brush reach/poses.
        Patch->WipeMask[32*FMCCoffeeWipe::Size+32]=Remaining;
    }
    bool Finish(AMCMouthSurface* Patch)
    {return Patch->CleanLiquidAt(Player.Hero,Patch->GetActorLocation(),80,.1f,16,EMCStainCleanReason::Brush);}
    double Now() const {return State->GetServerWorldTimeSeconds();}
    void Advance(float Seconds)
    {
        // UWorld clamps a frame to MaxUndilatedFrameTime (normally .4s).
        // Advance the actual gameplay clock in ordinary frame-sized steps;
        // passing a multi-second delta once does not simulate that interval.
        for(double Remaining=Seconds;Remaining>UE_KINDA_SMALL_NUMBER;) {
            const float Step=float(FMath::Min(Remaining,.1));
            ++GFrameCounter;World->Tick(LEVELTICK_TimeOnly,Step);Remaining-=Step;
        }
    }
    AMCArenaTooth* DirtyTooth()
    {
        const FTransform Pose(FVector(700,0,10000));
        auto* Tooth=World->SpawnActorDeferred<AMCArenaTooth>(AMCArenaTooth::StaticClass(),Pose);
        FMCArenaToothSettings Settings;Settings.InitialCalculusEveryNthTooth=0;
        Tooth->Initialize(0,Settings);Tooth->FinishSpawning(Pose);Tooth->Status->ApplyCoffee(1);
        return Tooth;
    }
    AMCFoodActor* Food()
    {
        // Keep whole/fragment meshes and generated collision from a real saved
        // menu row. Breaking this fixture must not fall back to missing SM_Food.
        const auto* Menu=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_BreakfastMenu.DT_BreakfastMenu"));
        const auto* Row=Menu?Menu->FindRow<FMCFoodRow>(TEXT("Egg"),TEXT("Results delivery fixture")):nullptr;
        if(!Row) return nullptr;
        const FTransform Pose(FVector(0,0,11000));
        auto* Item=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Item) return nullptr;
        FRandomStream Random(57);Item->ConfigureItem(TEXT("Egg"),*Row,Random,false);Item->FinishSpawning(Pose);
        Item->MarkManualThrow(Player.Hero);return Item;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCResultStainCompletionTest,"MessControl.Perks.Results.StainCompletionIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCResultStainCompletionTest::RunTest(const FString&)
{
    MCResultPassiveTests::FWorld T;auto& P=T.Player;
    if(!TestNotNull(TEXT("Authoritative native result subsystem exists"),T.Results)) return false;
    TestTrue(TEXT("Acquire cleaning recovery"),P.Perks->ServerGrantPerk(TEXT("B2_CleanBreath")));
    TestTrue(TEXT("Acquire completion speed"),P.Perks->ServerGrantPerk(TEXT("B2_CleanAcceleration")));
    T.Health(P,40);auto* Patch=T.Surface();
    TestTrue(TEXT("A partial native mask dose is accepted"),Patch->CleanLiquidAt(P.Hero,Patch->GetActorLocation(),80,.001f,.3f));
    TestFalse(TEXT("Partial work leaves the stain dirty"),Patch->IsClean());
    TestEqual(TEXT("Partial removal does not heal"),P.Hero->Status->State.Health,40.f);
    TestEqual(TEXT("Partial removal does not start completion speed"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.f);
    TestTrue(TEXT("Actual final mask cleanup completes the stain"),T.Finish(Patch));
    TestTrue(TEXT("Native care state reaches clean"),Patch->IsClean());
    TestEqual(TEXT("All coffee layers together award exactly3HP"),P.Hero->Status->State.Health,43.f);
    TestEqual(TEXT("Completion starts10percent speed"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.1f,.0001f);
    const double FirstExpiry=P.Perks->CleanSpeedUntil;
    TestEqual(TEXT("Completion stores exactly three seconds on the server gameplay clock"),FirstExpiry-T.Now(),3.,.0001);
    T.Advance(.5f);
    TestFalse(TEXT("A repeated clean callback is a no-op"),T.Finish(Patch));
    TestEqual(TEXT("No repeat heal"),P.Hero->Status->State.Health,43.f);
    TestEqual(TEXT("No repeat timer refresh"),P.Perks->CleanSpeedUntil,FirstExpiry);
    T.Redirty(Patch);TestTrue(TEXT("New actual dirt permits another completion"),T.Finish(Patch));
    TestEqual(TEXT("New stain episode heals again"),P.Hero->Status->State.Health,46.f);
    TestTrue(TEXT("New completion refreshes duration"),P.Perks->CleanSpeedUntil>FirstExpiry);
    const double RefreshedCleanExpiry=P.Perks->CleanSpeedUntil;
    TestEqual(TEXT("A new completion refreshes a full three-second interval"),RefreshedCleanExpiry-T.Now(),3.,.0001);
    T.Advance(3.2f);
    TestTrue(TEXT("The actual server gameplay clock passes the captured clean expiry"),T.Now()>RefreshedCleanExpiry);
    TestEqual(TEXT("Completion speed expires after three seconds"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.f);
    auto* Technical=T.Surface(FVector(500,0,10000));
    const auto Receipt=T.Results->BeginCleaning(Technical);Technical->ClearLiquidForBooster();
    TestFalse(TEXT("Technical cleanup cannot award recovery"),T.Results->CompleteCleaning(Receipt,P.Hero,EMCStainCleanReason::Technical,true));
    TestFalse(TEXT("The same consumed receipt cannot be relabeled and replayed"),T.Results->CompleteCleaning(Receipt,P.Hero,EMCStainCleanReason::Brush,true));
    TestEqual(TEXT("Technical cleanup leaves player HP unchanged"),P.Hero->Status->State.Health,46.f);
    auto* Air=T.Surface(FVector(1000,0,10000));Air->WipeMask.Init(255,FMCCoffeeWipe::Count);
    TestFalse(TEXT("Changing only mask bytes outside the deposited visible stain is empty work"),
        Air->CleanLiquidAt(P.Hero,Air->GetActorLocation()+FVector(90,90,0),20,.1f,16));
    TestEqual(TEXT("Empty spatial work cannot grant a recovery reward"),P.Hero->Status->State.Health,46.f);
    Air->ClearLiquidForBooster();
    auto* Tooth=T.DirtyTooth();
    TestTrue(TEXT("A native whole tooth coat can be completed once"),Tooth->CleanGrimeFromPassive(P.Hero));
    TestEqual(TEXT("A whole tooth coat is one3HP stain, not its coffee layers"),P.Hero->Status->State.Health,49.f);
    TestFalse(TEXT("A complete tooth cannot award a second completion"),Tooth->CleanGrimeFromPassive(P.Hero));
    P.Perks->RemovePerk(TEXT("B2_CleanAcceleration"));P.Perks->ServerGrantPerk(TEXT("B2_CleanAcceleration"));
    TestEqual(TEXT("Remove/regrant cannot resurrect a completion timer"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.f);
    P.Perks->ResetPerks();TestTrue(TEXT("Run reset clears the player completion timer"),P.Perks->CleanSpeedUntil<0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCResultMouthTransitionTest,"MessControl.Perks.Results.TrueMouthTransitions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCResultMouthTransitionTest::RunTest(const FString&)
{
    MCResultPassiveTests::FWorld T;auto& A=T.Player;auto B=T.AddPlayer(200);
    A.Perks->ServerGrantPerk(TEXT("B2_RefreshingWork"));B.Perks->ServerGrantPerk(TEXT("B2_RefreshingWork"));
    T.Health(A,40);T.Health(B,50);
    auto* First=T.Surface();auto* Last=T.Surface(FVector(600,0,10000));
    T.Finish(First);
    TestEqual(TEXT("Another true dirty mask blocks global recovery"),A.Hero->Status->State.Health,40.f);
    T.Finish(Last);
    TestEqual(TEXT("Final mouth cleanup heals the working owner20percent"),A.Hero->Status->State.Health,60.f);
    TestEqual(TEXT("Final mouth cleanup heals another owner from their own maximumHP"),B.Hero->Status->State.Health,90.f);
    T.Finish(Last);T.Advance(.1f);
    TestEqual(TEXT("Remaining at100percent cannot repeat the reward"),A.Hero->Status->State.Health,60.f);
    auto C=T.AddPlayer();T.Health(C,25);C.Perks->ServerGrantPerk(TEXT("B2_RefreshingWork"));
    TestEqual(TEXT("Acquiring the passive in an already clean mouth is inert"),C.Hero->Status->State.Health,25.f);
    T.Redirty(First);T.Finish(First);
    TestEqual(TEXT("Immediate redirty/reclean works without a hidden15second cooldown"),A.Hero->Status->State.Health,80.f);
    TestEqual(TEXT("A newly owning player participates in the next transition"),C.Hero->Status->State.Health,45.f);
    auto* Tooth=T.DirtyTooth();T.Redirty(Last);T.Finish(Last);
    TestEqual(TEXT("A dirty available tooth blocks the true mouth predicate"),A.Hero->Status->State.Health,80.f);
    Tooth->CleanGrimeFromPassive(A.Hero);
    TestEqual(TEXT("Finishing the remaining tooth causes the next genuine transition"),A.Hero->Status->State.Health,100.f);
    TestEqual(TEXT("Global recovery respects each maximumHP"),B.Hero->Status->State.Health,170.f);
    auto* Technical=T.Surface(FVector(1100,0,10000));Technical->ClearLiquidForBooster();
    TestEqual(TEXT("Technical disappearance of the last dirt never fabricates a cleaning event"),C.Hero->Status->State.Health,65.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCResultNeighborTest,"MessControl.Perks.Results.NeighborNoChain",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCResultNeighborTest::RunTest(const FString&)
{
    MCResultPassiveTests::FWorld T;auto& P=T.Player;
    T.Parameter(TEXT("B2_NeatFreak"),TEXT("Chance"),1);T.Parameter(TEXT("B2_NeatFreak"),TEXT("Radius"),300);
    P.Perks->ServerGrantPerk(TEXT("B2_NeatFreak"));P.Perks->ServerGrantPerk(TEXT("B2_CleanBreath"));
    P.Perks->ServerGrantPerk(TEXT("B2_CleanAcceleration"));T.Health(P,50);
    auto* Origin=T.Surface();auto* Nearest=T.Surface(FVector(100,0,10000));
    auto* Next=T.Surface(FVector(180,0,10000));auto* Far=T.Surface(FVector(1000,0,10000));
    TestTrue(TEXT("The direct stain finishes"),T.Finish(Origin));
    TestTrue(TEXT("One nearest eligible neighbor finishes"),Nearest->IsClean());
    TestFalse(TEXT("A bonus completion cannot chain into another nearby stain"),Next->IsClean());
    TestFalse(TEXT("Distant dirt is untouched"),Far->IsClean());
    TestEqual(TEXT("Direct and authorized bonus completion each heal once"),P.Hero->Status->State.Health,56.f);
    TestEqual(TEXT("Bonus completion refreshes speed without doubling its magnitude"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.1f,.0001f);
    T.Finish(Next);
    TestFalse(TEXT("The radius gate still excludes the distant neighbor"),Far->IsClean());
    TestEqual(TEXT("No eligible neighbor means only the direct heal"),P.Hero->Status->State.Health,59.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCResultCleaningRatesTest,"MessControl.Perks.Results.CleaningRatesAndThreshold",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCResultCleaningRatesTest::RunTest(const FString&)
{
    MCResultPassiveTests::FWorld T;auto& P=T.Player;
    auto* Ordinary=T.Surface(FVector(0,0,10000),83);auto* Improved=T.Surface(FVector(500,0,10000),83);
    TestTrue(TEXT("Fixture uses a measured mask fraction below15percent"),Ordinary->RemainingLiquid()>0 && Ordinary->RemainingLiquid()<.15f);
    Ordinary->CleanLiquidAt(P.Hero,Ordinary->GetActorLocation(),80,.001f,2.8f);
    P.Perks->ServerGrantPerk(TEXT("B2_LastStroke"));
    TestEqual(TEXT("Below the threshold multiplies combined brush/softener rate"),UMCResultPassives::CleaningRate(P.Hero,.1499f,2.8f),5.6f,.0001f);
    TestEqual(TEXT("Exactly15percent receives no last-stroke bonus"),UMCResultPassives::CleaningRate(P.Hero,.15f,2.8f),2.8f,.0001f);
    Improved->CleanLiquidAt(P.Hero,Improved->GetActorLocation(),80,.001f,2.8f);
    TestTrue(TEXT("The actual native mask accepts the combined rate above the old2x cap"),Improved->RemainingLiquid()<Ordinary->RemainingLiquid());
    TArray<uint8> Slow,Normal,VolumeSlow,VolumeNormal;TArray<float> FS,FN,VS,VN;
    FMCCoffeeWipe::Reset(Slow);Normal=Slow;FMCSurfaceWipe::Reset(VolumeSlow);VolumeNormal=VolumeSlow;
    FMCCoffeeWipe::Stroke(Slow,FVector2D(.5),FVector2D(.5),.2f,.01f,&FS,.3f);
    FMCCoffeeWipe::Stroke(Normal,FVector2D(.5),FVector2D(.5),.2f,.01f,&FN,1.f);
    FMCSurfaceWipe::Stroke(VolumeSlow,FVector(.5),FVector(.5),FVector(1),1,.01f,&VS,.3f);
    FMCSurfaceWipe::Stroke(VolumeNormal,FVector(.5),FVector(.5),FVector(1),1,.01f,&VN,1.f);
    TestTrue(TEXT("30percent residual does less real puddle work than normal"),Slow[32*64+32]>Normal[32*64+32]);
    const int32 Center=FMCSurfaceWipe::Index(8,8,8);
    TestTrue(TEXT("30percent residual does less real enamel work than normal"),VolumeSlow[Center]>VolumeNormal[Center]);
    const auto Unchanged=Slow;
    for(float Invalid:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN()})
        TestFalse(TEXT("Zero, negative and nonfinite dose is rejected without mask work"),FMCCoffeeWipe::Stroke(Slow,FVector2D(.5),FVector2D(.5),.2f,.01f,&FS,Invalid));
    TestTrue(TEXT("Rejected doses preserve all actual mask bytes"),Slow==Unchanged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCResultDeliveryTest,"MessControl.Perks.Results.DeliveryIdentityAndLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCResultDeliveryTest::RunTest(const FString&)
{
    MCResultPassiveTests::FWorld T;auto& P=T.Player;P.Perks->ServerGrantPerk(TEXT("B2_ReturnTrip"));
    auto* First=T.Food();
    if(!TestNotNull(TEXT("The real saved Egg menu row creates a valid food fixture"),First)) return false;
    TestNotNull(TEXT("The authored whole food mesh is loaded"),First->ItemMesh.Get());
    TestTrue(TEXT("The authored fragment mesh choices are retained for breaking"),!First->FoodData.FragmentMeshes.IsEmpty());
    First->AwardDelivery();
    TestEqual(TEXT("Actual native delivery receipt grants15percent speed"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.15f,.0001f);
    const double FirstExpiry=P.Perks->DeliverySpeedUntil;
    TestEqual(TEXT("Delivery stores exactly two seconds on the server gameplay clock"),FirstExpiry-T.Now(),2.,.0001);
    T.Advance(.5f);First->AwardDelivery();
    TestEqual(TEXT("The same food cannot refresh a sealed delivery"),P.Perks->DeliverySpeedUntil,FirstExpiry);
    auto* Second=T.Food();Second->AwardDelivery();
    TestTrue(TEXT("Another actual delivery refreshes the two-second term"),P.Perks->DeliverySpeedUntil>FirstExpiry);
    TestEqual(TEXT("Refresh keeps the same15percent magnitude"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.15f,.0001f);
    const double Refreshed=P.Perks->DeliverySpeedUntil;
    TestEqual(TEXT("Another delivery refreshes a full two-second interval"),Refreshed-T.Now(),2.,.0001);
    auto* Destroyed=T.Food();Destroyed->FoodData.Fragments=1;Destroyed->Health=1;
    Destroyed->HitFood(100,FVector::ForwardVector,P.Hero);
    TestTrue(TEXT("The whole food actually breaks and disposes"),Destroyed->IsDisposed());
    TestEqual(TEXT("Breaking food is not a delivery"),P.Perks->DeliverySpeedUntil,Refreshed);
    auto* Foreign=T.Food();Foreign->FoodData.Kind=EMCFoodKind::ForeignObject;Foreign->AwardDelivery();
    TestEqual(TEXT("Foreign-object disposal cannot count as food delivery"),P.Perks->DeliverySpeedUntil,Refreshed);
    auto* Brush=T.Food();Brush->bBrushTool=true;Brush->AwardDelivery();
    TestEqual(TEXT("Brush disposal cannot count as food delivery"),P.Perks->DeliverySpeedUntil,Refreshed);
    auto* Old=P.Hero;P.Controller->UnPossess();
    P.Hero=T.World->SpawnActor<AMCToothCharacter>(FVector(0,0,12000),FRotator::ZeroRotator);P.Controller->Possess(P.Hero);
    P.Hero->GetCharacterMovement()->DisableMovement();Old->Destroy();
    TestEqual(TEXT("Replacing the pawn on the same PlayerState retains the timed player buff"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.15f,.0001f);
    P.Perks->RemovePerk(TEXT("B2_ReturnTrip"));P.Perks->ServerGrantPerk(TEXT("B2_ReturnTrip"));
    TestEqual(TEXT("Remove/regrant cannot resurrect an old delivery timer"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.f);
    T.Food()->AwardDelivery();const double FinalDeliveryExpiry=P.Perks->DeliverySpeedUntil;
    TestEqual(TEXT("The final delivery still uses exactly two seconds"),FinalDeliveryExpiry-T.Now(),2.,.0001);
    T.Advance(2.2f);
    TestTrue(TEXT("The actual server gameplay clock passes the captured delivery expiry"),T.Now()>FinalDeliveryExpiry);
    TestEqual(TEXT("The delivery bonus expires after two seconds"),P.Perks->RogueValue(EMCRogueStat::NormalSpeed),1.f);
    P.Perks->ResetPerks();TestTrue(TEXT("Run reset clears delivery timing"),P.Perks->DeliverySpeedUntil<0);
    return true;
}
#endif
