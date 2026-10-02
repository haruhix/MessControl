#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCInventoryComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace {
struct FWeaponHitWorld {
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter *Attacker=nullptr,*Victim=nullptr;
    FWeaponHitWorld() {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
        W->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
        Attacker=W->SpawnActor<AMCToothCharacter>(FVector(0,0,2000),FRotator::ZeroRotator);
        Victim=W->SpawnActor<AMCToothCharacter>(FVector(125,0,2000),FRotator::ZeroRotator);
        Attacker->GetCharacterMovement()->DisableMovement();Victim->GetCharacterMovement()->DisableMovement();
        Victim->Status->Initialize(1000);
        Step(.8f);
    }
    ~FWeaponHitWorld() {W->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(W);W->DestroyWorld(false);}
    void Step(float Seconds,float Dt=1.f/60) {
        for(int32 I=0;I<FMath::CeilToInt(Seconds/Dt);++I) {++GFrameCounter;W->Tick(LEVELTICK_All,Dt);}
    }
    void Wall() {
        auto* A=W->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(A);
        A->SetRootComponent(Box);Box->SetBoxExtent(FVector(5,200,300));Box->SetCollisionProfileName(TEXT("BlockAll"));
        Box->RegisterComponent();A->SetActorLocation(FVector(60,0,2000));
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWeaponPlayerDamage,"MessControl.Inventory.PlayerHits.AllWeaponsExceptSpray",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWeaponPlayerDamage::RunTest(const FString&) {
    for(float Dt:{1.f/30,1.f/60,1.f/120}) for(EMCToolSlot Slot:{EMCToolSlot::Brush,EMCToolSlot::Pickaxe,EMCToolSlot::Knife,EMCToolSlot::Spray}) {
        FWeaponHitWorld T;T.Attacker->Inventory->ServerSelect(Slot);
        const float Damage=Slot==EMCToolSlot::Spray?0:Slot==EMCToolSlot::Brush?25:T.Attacker->Inventory->Damage();
        T.Attacker->SwingBrush();T.Attacker->SwingBrush();T.Step(.65f,Dt);
        TestEqual(*FString::Printf(TEXT("Tool %d applies exactly one configured damage amount at %.0f FPS"),int32(Slot),1/Dt),T.Victim->Status->State.Health,1000-Damage);
        TestEqual(TEXT("One swing produces at most one confirmed hit, including knife follow-through"),T.Attacker->ConfirmedHitCount,Slot==EMCToolSlot::Spray?0:1);
        TestEqual(TEXT("Repeated input cannot create a second offensive swing"),T.Attacker->ValidatedSwingCount,Slot==EMCToolSlot::Spray?0:1);
        TestEqual(TEXT("The attacker never damages itself"),T.Attacker->Status->State.Health,100.f);
        if(Slot!=EMCToolSlot::Spray) TestEqual(TEXT("Offensive tools retain the physical knockdown"),T.Victim->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
        else TestEqual(TEXT("Spray cannot knock another player down"),T.Victim->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
    }
    FWeaponHitWorld T;T.Attacker->Inventory->UnlockWaterJet();T.Attacker->SwingBrush();T.Step(.65f);
    TestEqual(TEXT("The upgraded first slot keeps its player hit"),T.Victim->Status->State.Health,975.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCWeaponPlayerProtection,"MessControl.Inventory.PlayerHits.ReachWallsAndLateKnifeContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCWeaponPlayerProtection::RunTest(const FString&) {
    for(EMCToolSlot Slot:{EMCToolSlot::Brush,EMCToolSlot::Pickaxe,EMCToolSlot::Knife}) for(int32 Case=0;Case<5;++Case) {
        FWeaponHitWorld T;T.Attacker->Inventory->ServerSelect(Slot);
        if(Case==0) T.Wall();
        if(Case==1) T.Victim->SetActorLocation(FVector(-125,0,2000));
        if(Case==2) T.Victim->SetActorLocation(FVector(250,0,2000));
        if(Case==3) T.Victim->SetActorLocation(FVector(125,0,2150));
        if(Case==4) T.Victim->Status->Damage(1000,FVector::ForwardVector);
        const float Before=T.Victim->Status->State.Health;
        T.Attacker->SwingBrush();T.Step(.65f);
        TestEqual(TEXT("Walls, direction, reach, height and dead players reject weapon hits"),T.Victim->Status->State.Health,Before);
        TestEqual(TEXT("A rejected player hit produces no success event"),T.Attacker->ConfirmedHitCount,0);
    }
    for(float Dt:{1.f/30,1.f/60,1.f/120}) {
        FWeaponHitWorld T;T.Attacker->Inventory->ServerSelect(EMCToolSlot::Knife);
        T.Victim->SetActorLocation(FVector(500,0,2000));T.Attacker->SwingBrush();T.Step(.30f,Dt);
        TestEqual(TEXT("The first knife sample misses a distant player"),T.Attacker->ConfirmedHitCount,0);
        T.Victim->SetActorLocation(FVector(125,0,2000));T.Step(.35f,Dt);
        TestEqual(TEXT("A player entering the knife follow-through receives only one hit"),T.Victim->Status->State.Health,1000-T.Attacker->Inventory->Damage());
        TestEqual(TEXT("Late knife contact ends the retry window"),T.Attacker->ConfirmedHitCount,1);
    }
    return true;
}
#endif
