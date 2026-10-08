#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCInventoryComponent.h"
#include "MCRewardChest.h"
#include "MCRewardDropZone.h"
#include "MCPlayerState.h"
#include "MCPlayerController.h"
#include "MCPerkComponent.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "MCMouthSurface.h"
#include "MCFirePatch.h"
#include "MCSurfaceWipe.h"
#include "MCCoffeeWipe.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"

namespace {
struct FToolWorld {
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false);
    AMCGameState* GS=nullptr;AMCToothCharacter *A=nullptr,*B=nullptr;
    FToolWorld() {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);W->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
        GS=W->SpawnActor<AMCGameState>();GS->Phase=EMCShiftPhase::Working;GS->bLobbyWaiting=false;W->SetGameState(GS);
        A=Worker(FVector(0,0,10000));B=Worker(FVector(1000,0,10000));Step(.1f);
    }
    ~FToolWorld() {W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}
    AMCToothCharacter* Worker(FVector P) {
        auto* PC=W->SpawnActor<AMCPlayerController>();auto* PS=W->SpawnActor<AMCPlayerState>();PC->SetPlayerState(PS);
        auto* H=W->SpawnActor<AMCToothCharacter>(P,FRotator::ZeroRotator);PC->Possess(H);H->GetCharacterMovement()->DisableMovement();H->Status->Initialize(1000);return H;
    }
    void Step(float S) {for(int32 I=0;I<FMath::CeilToInt(S*60);++I) {++GFrameCounter;W->Tick(LEVELTICK_All,1.f/60);}}
    UMCPerkComponent* Perks(AMCToothCharacter* H) {return H->GetPlayerState<AMCPlayerState>()->Perks;}
};
FName ToolID(const TCHAR* Tool,bool Legend=false) {return FName(*FString::Printf(TEXT("Tool_%s_%s"),Tool,Legend?TEXT("Legendary"):TEXT("Rare")));}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolBoosterOwnership,"MessControl.ToolBoosters.OwnershipAndChest",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolBoosterOwnership::RunTest(const FString&) {
    FToolWorld T;auto* PA=T.Perks(T.A);auto* PB=T.Perks(T.B);
    TestTrue(TEXT("Rare reward grants to opener"),PA->ServerGrantReward(ToolID(TEXT("MeshaBrush"))));
    TestTrue(TEXT("Rare brush is equipped by opener"),T.A->Inventory->HasUpgrade(EMCToolUpgrade::MeshaBrush));
    TestFalse(TEXT("Rare brush does not leak to a teammate"),T.B->Inventory->HasUpgrade(EMCToolUpgrade::MeshaBrush));
    TestTrue(TEXT("Legendary can promote an existing rare tool"),PA->ServerGrantReward(ToolID(TEXT("MeshaBrush"),true)));
    TestTrue(TEXT("Legendary reaches existing teammates"),PB->HasToolUpgrade(EMCToolUpgrade::MeshaBrush));
    auto* Late=T.Worker(FVector(2000,0,10000));TestTrue(TEXT("Late join and replacement pawns retain the team upgrade"),Late->Inventory->HasUpgrade(EMCToolUpgrade::MeshaBrush));
    TestFalse(TEXT("A team tool cannot drop again as a personal downgrade"),PB->CanGrantPerk(ToolID(TEXT("MeshaBrush"))));
    TestFalse(TEXT("A repeated legendary is excluded"),PA->CanGrantPerk(ToolID(TEXT("MeshaBrush"),true)));
    TestEqual(TEXT("Promotion does not multiply cleaning twice"),T.A->Inventory->CleaningSpeedMultiplier(),2.f);
    T.GS->TeamToolUpgrades=0;PA->ResetPerks();PB->ResetPerks();
    auto* Zone=T.W->SpawnActor<AMCRewardDropZone>();
    for(bool Legend:{false,true}) {
        auto* C=T.W->SpawnActor<AMCRewardChest>(FVector(0,0,10000),FRotator::ZeroRotator);
        C->InitializeReward(C->GetActorLocation(),C->GetActorLocation(),47,PA->GetPerkTable(),EMCRewardSelectionPolicy::ChooseOne,Zone);
        C->ForceMimicForTest(false);C->Stage=EMCRewardChestStage::Landed;
        C->LegendaryToolChancePercent=Legend?100:0;C->RareToolChancePercent=Legend?0:100;
        const auto Contact=C->GetLockpickContact();FVector P=Contact.GetLocation()+Contact.GetUnitAxis(EAxis::X)*120;P.Z=10061;
        T.A->SetActorLocation(P,false,nullptr,ETeleportType::TeleportPhysics);
        if(!TestTrue(TEXT("Real opener enters chest selection"),C->BeginLockpicking(T.A))) {C->Destroy();continue;}
        int32 ToolIndex=INDEX_NONE;FMCPerkDefinition Row;
        for(int32 I=0;I<C->LootIDs.Num();++I) if(PA->GetPerkDefinition(C->LootIDs[I],Row) && Row.ToolUpgrade!=EMCToolUpgrade::None) ToolIndex=I;
        TestEqual(TEXT("Booster retains three choose-one cards"),C->LootIDs.Num(),3);
        if(TestTrue(TEXT("Forced rarity produces a selectable tool"),ToolIndex!=INDEX_NONE)) {
            PA->GetPerkDefinition(C->LootIDs[ToolIndex],Row);C->Stage=EMCRewardChestStage::Open;
            TestEqual(TEXT("Actual chest offer uses the rolled rarity"),Row.Rarity,Legend?EMCPerkRarity::Legendary:EMCPerkRarity::Rare);
            TestTrue(TEXT("Selected card grants through real chest claim"),C->TryChooseCard(T.A,ToolIndex));
            TestEqual(TEXT("Claim scope matches rarity"),PB->HasToolUpgrade(Row.ToolUpgrade),Legend);
        }
        C->Destroy();PA->ResetPerks();PB->ResetPerks();T.GS->TeamToolUpgrades=0;
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolBoosterChance,"MessControl.ToolBoosters.Probability",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolBoosterChance::RunTest(const FString&) {
    int32 Legendary=0,Rare=0;
    for(int32 I=0;I<1000000;++I) {
        const auto R=AMCRewardChest::RollToolRarity(I,.02f,1.f);Legendary+=R==EMCPerkRarity::Legendary;Rare+=R==EMCPerkRarity::Rare;
    }
    AddInfo(FString::Printf(TEXT("One million chest seeds: legendary=%d (0.02%% target), rare=%d (1%% target)"),Legendary,Rare));
    TestTrue(TEXT("Legendary is 0.02 percent, not two percent"),Legendary>=190 && Legendary<=210);
    TestTrue(TEXT("Rare roll stays independent at one percent"),Rare>=9900 && Rare<=10100);
    TestEqual(TEXT("Zero chances preserve ordinary loot"),AMCRewardChest::RollToolRarity(42,0,0),EMCPerkRarity::Standard);
    TestEqual(TEXT("Legendary roll is stable for a chest seed"),AMCRewardChest::RollToolRarity(123,.02f,1),AMCRewardChest::RollToolRarity(123,.02f,1));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolBoosterMechanics,"MessControl.ToolBoosters.Mechanics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolBoosterMechanics::RunTest(const FString&) {
    FToolWorld T;auto* I=T.A->Inventory.Get();auto* P=T.Perks(T.A);
    P->ServerGrantReward(ToolID(TEXT("MeshaBrush")));TestEqual(TEXT("Brush cleans twice as fast"),I->CleaningSpeedMultiplier(),2.f);
    TestEqual(TEXT("Brush contact follows a large dirt mesh"),I->CleaningRadius(180),180.f);
    P->ServerGrantReward(ToolID(TEXT("Buffer")));I->ServerSelect(EMCToolSlot::Pickaxe);
    TestEqual(TEXT("Heavy cleaner halves locomotion speed"),I->MovementMultiplier(),.5f);
    I->ServerSelect(EMCToolSlot::Brush);TestEqual(TEXT("Weight ends when stowing the buffer"),I->MovementMultiplier(),1.f);
    P->ServerGrantReward(ToolID(TEXT("Chainsaw")));I->ServerSelect(EMCToolSlot::Knife);T.A->ServerSetPrimary(true);
    TestTrue(TEXT("Held input starts the continuous saw"),I->IsChainsawRunning());TestEqual(TEXT("Saw speeds up locomotion"),I->MovementMultiplier(),1.4f);
    T.B->SetActorLocation(T.A->GetActorLocation()+FVector(130,0,0));I->SawContact();
    TestEqual(TEXT("Saw hurts allies twice as much as the pickaxe"),T.B->Status->State.Health,1000-I->Settings->PickaxeDamage*2);
    const float Hurt=T.B->Status->State.Health;I->SawContact();TestEqual(TEXT("Saw cannot damage an ally every frame"),T.B->Status->State.Health,Hurt);
    T.B->SetActorLocation(FVector(1500,0,10000));
    auto* Food=T.W->SpawnActor<AMCFoodActor>(T.A->GetActorLocation()+FVector(120,0,0),FRotator::ZeroRotator);
    Food->Body->SetSimulatePhysics(false);Food->FoodData.Kind=EMCFoodKind::Food;Food->FoodData.Resistance=EMCFoodResistance::Hard;Food->FoodData.Health=200;Food->Health=200;Food->FoodData.Fragments=0;
    TestTrue(TEXT("The saw also accepts hard vegetables such as carrot"),I->CanBreak(Food));
    I->SawContact();TestEqual(TEXT("First contact removes half the vegetable health"),Food->Health,100.f);
    T.Step(.5f);I->SawContact();TestTrue(TEXT("Second contact cuts the vegetable"),Food->IsDisposed());
    T.A->GetCharacterMovement()->Velocity=FVector(350,0,0);FHitResult Wall;Wall.ImpactNormal=FVector(-1,0,0);Wall.ImpactPoint=T.A->GetActorLocation()+FVector(60,0,0);
    I->HandleChainsawCollision(nullptr,Wall);
    TestEqual(TEXT("Frontal wall crash damages the user"),T.A->Status->State.Health,970.f);
    TestEqual(TEXT("Wall crash causes actual knockdown"),T.A->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
    TestFalse(TEXT("Crash cancels the saw input"),T.A->IsPrimaryHeld());
    // A fresh standing user exercises held charge, release, cooldown and cancelled mode switches.
    auto* Shooter=T.Worker(FVector(-1000,0,10000));I=Shooter->Inventory;T.Perks(Shooter)->ServerGrantReward(ToolID(TEXT("Watergun")));
    I->ServerSelect(EMCToolSlot::Spray);TestEqual(TEXT("Care reaches ten metres"),I->SprayReach(),1000.f);
    auto* Ulcer=T.W->SpawnActor<AMCMouthSurface>(Shooter->GetActorLocation()+FVector(700,0,-40),FRotator::ZeroRotator);Ulcer->bUlcer=true;
    Shooter->ServerSetPrimary(true);T.Step(.3f);TestTrue(TEXT("Watergun heals a distant ulcer"),Ulcer->Healing>.09f);
    Shooter->ServerSetPrimary(false);I->ServerSelect(EMCToolSlot::Spray);TestTrue(TEXT("Repeat slot four switches to pressure"),I->bPressureMode);
    Shooter->ServerSetPrimary(true);T.Step(.5f);TestTrue(TEXT("Holding primary charges the shot"),I->bChargingWater && I->WaterChargeFraction()>.25f);
    Shooter->ServerSetPrimary(false);T.Step(.02f);TestTrue(TEXT("Release fires and begins four second cooldown"),!I->bChargingWater && I->SpraySecondsLeft()>3.9f && I->SpraySecondsLeft()<=4.f);
    Shooter->ServerSetPrimary(true);T.Step(.2f);TestFalse(TEXT("Holding during cooldown cannot charge early"),I->bChargingWater);
    I->ServerSelect(EMCToolSlot::Spray);TestFalse(TEXT("Mode change cancels input without firing"),Shooter->IsPrimaryHeld() || I->bChargingWater || I->bPressureMode);
    Ulcer->Destroy();auto* Target=T.Worker(I->SprayOrigin()+Shooter->GetActorForwardVector()*450);
    I->WaterShotReadyAt=0;I->FireChargedWater(0);TestEqual(TEXT("An uncharged water shot damages its swept target"),Target->Status->State.Health,970.f);
    I->WaterShotReadyAt=0;I->FireChargedWater(1);TestEqual(TEXT("Holding to full charge increases actual damage fourfold"),Target->Status->State.Health,850.f);
    // A camera looking sideways must aim independently of the body's facing.
    const FVector View=Shooter->GetActorLocation()+FVector(-300,0,100);
    Target->SetActorLocation(Shooter->GetActorLocation()+FVector(0,400,0));
    I->ServerCommitSprayView(View,(Target->GetActorLocation()-View).GetSafeNormal());
    I->WaterShotReadyAt=0;I->FireChargedWater(1);
    TestEqual(TEXT("Camera-centre water shot hits a target beside the character"),Target->Status->State.Health,730.f);
    const FVector Aim=I->SprayAim();
    I->ServerCommitSprayView(View+FVector(10000,0,0),FVector::UpVector);
    TestTrue(TEXT("Out-of-range camera origins cannot replace the accepted aim"),I->SprayAim().Equals(Aim,1.f));
    Target->Destroy();I->bPressureMode=false;
    auto* AimedUlcer=T.W->SpawnActor<AMCMouthSurface>(Shooter->GetActorLocation()+FVector(0,500,-40),FRotator::ZeroRotator);AimedUlcer->bUlcer=true;
    auto* OffAimUlcer=T.W->SpawnActor<AMCMouthSurface>(Shooter->GetActorLocation()+FVector(180,0,-40),FRotator::ZeroRotator);OffAimUlcer->bUlcer=true;
    I->ServerCommitSprayView(View,(AimedUlcer->GetActorLocation()+FVector(0,0,10)-View).GetSafeNormal());
    Shooter->ServerSetPrimary(true);T.Step(.3f);
    TestTrue(TEXT("Care follows the aimed ulcer beside the body"),AimedUlcer->Healing>.09f);
    TestEqual(TEXT("A closer ulcer outside the crosshair receives no treatment"),OffAimUlcer->Healing,0.f);
    Shooter->ServerSetPrimary(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCToolBrushRates,"MessControl.ToolBoosters.BrushRateAcrossFrames",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCToolBrushRates::RunTest(const FString&) {
    for(float Dt:{.1f,1.f/30,1.f/60,1.f/120}) {
        TArray<uint8> Normal,Fast;TArray<float> PN,PF;FMCSurfaceWipe::Reset(Normal);FMCSurfaceWipe::Reset(Fast);
        const FVector At=FVector(8,8,8)/15.;const int32 I=FMCSurfaceWipe::Index(8,8,8);
        FMCSurfaceWipe::Stroke(Normal,At,At,FVector(200),36,Dt,&PN,1);
        FMCSurfaceWipe::Stroke(Fast,At,At,FVector(200),36,Dt,&PF,2);
        TestTrue(TEXT("Actual tooth mask receives double cleaning even at ten FPS"),FMath::IsNearlyEqual(255-PF[I],2*(255-PN[I]),.001f));
        FMCCoffeeWipe::Reset(Normal);FMCCoffeeWipe::Reset(Fast);PN.Reset();PF.Reset();const FVector2D UV(.5,.5);const int32 J=32*64+32;
        FMCCoffeeWipe::Stroke(Normal,UV,UV,.5f,Dt,&PN,1);FMCCoffeeWipe::Stroke(Fast,UV,UV,.5f,Dt,&PF,2);
        const float N=(Normal[J]+PN[J])/255.f,F=(Fast[J]+PF[J])/255.f;
        TestTrue(TEXT("Actual puddle removal rate doubles after validating elapsed time"),FMath::IsNearlyEqual(F,N*N,.0001f));
        TestTrue(TEXT("Large boosted brush reaches the outer dirt footprint"),Normal[32*64+58]==255 && Fast[32*64+58]<255);
    }
    return true;
}
#endif
