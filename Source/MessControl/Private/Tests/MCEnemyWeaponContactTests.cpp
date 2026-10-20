#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCBossCharacter.h"
#include "MCCataclysmJelly.h"
#include "MCCombatFeedbackComponent.h"
#include "MCInventoryComponent.h"
#include "MCIceEvent.h"
#include "MCNutBoss.h"
#include "MCNutEnemy.h"
#include "MCNutKamikaze.h"
#include "MCSmallMintCandy.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodySetup.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCEnemyWeaponContactTests
{
enum class EEnemy { Nut, Summon, LargeJelly, MediumJelly, SmallJelly, Knight, Mage, OtherBoss, IceCandy, SmallMint };
void FinishMesh(UStaticMesh* Mesh)
{
#if WITH_EDITOR
    if(Mesh) FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
}
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCToothCharacter* Hero=nullptr;
    AActor* Enemy=nullptr;
    FWorld(EEnemy Kind,EMCToolSlot Tool=EMCToolSlot::Knife,bool bAuthoredCandy=false)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(700,0,2000),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();
        Hero->Status->Settings.Reach=115.f;
        Hero->Inventory->ServerSelect(Tool);
        const FTransform Pose(FVector(0,0,2000));
        if(Kind==EEnemy::OtherBoss) {
            auto* Boss=World->SpawnActor<AMCBossCharacter>(Pose.GetLocation(),FRotator::ZeroRotator);
            Boss->GetCharacterMovement()->DisableMovement();Boss->SetActorTickEnabled(false);
            Enemy=Boss;
            return;
        }
        auto* Tongue=World->SpawnActor<AMCTongue>();Tongue->SetActorTickEnabled(false);
        auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        if(Cube) FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        if(Kind==EEnemy::IceCandy || Kind==EEnemy::SmallMint) {
            // Synthetic overlap cases use a known convex cube. The authored
            // candy case keeps its saved mesh and tests its real convex hulls.
            if(Kind==EEnemy::IceCandy) {
                auto* Candy=World->SpawnActor<AMCIceEvent>(Pose.GetLocation(),FRotator::ZeroRotator);
                Candy->SetActorTickEnabled(false);Candy->Stage=EMCIceEventStage::Active;
                Candy->CandyHealth=Candy->MaxCandyHealth=500.f;
                if(!bAuthoredCandy) Candy->Body->SetStaticMesh(Cube);
                else FinishMesh(Candy->Body->GetStaticMesh());
                Candy->Body->SetWorldScale3D(FVector::OneVector);
                Candy->Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Enemy=Candy;
            } else {
                auto* Candy=World->SpawnActor<AMCSmallMintCandy>(Pose.GetLocation(),FRotator::ZeroRotator);
                Candy->SetActorTickEnabled(false);Candy->Stage=EMCSmallMintStage::Active;
                Candy->Health=Candy->MaxHealth=500.f;
                if(!bAuthoredCandy) Candy->Body->SetStaticMesh(Cube);
                else FinishMesh(Candy->Body->GetStaticMesh());
                Candy->Body->SetWorldScale3D(FVector::OneVector);
                Candy->Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Enemy=Candy;
            }
        } else if(Kind==EEnemy::Knight || Kind==EEnemy::Mage) {
            auto* Boss=World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            FMCNutBossSettings Settings;Settings.WholeMesh=Settings.KernelMesh=Settings.ShellMesh=Cube;
            Boss->ConfigureEncounter(Tongue,nullptr,Kind==EEnemy::Knight?EMCNutBossRole::Tank:EMCNutBossRole::Mage,Settings,1,91);
            Boss->FinishSpawning(Pose);Boss->SetActorTickEnabled(false);
            // The fixture starts after the entrance; ordinary weapon input and
            // the native shield/damage resolver remain responsible for contact.
            Boss->State=EMCNutBossState::Idle;Boss->SetActorLocation(Pose.GetLocation(),false);
            Enemy=Boss;
        } else if(Kind==EEnemy::LargeJelly || Kind==EEnemy::MediumJelly || Kind==EEnemy::SmallJelly) {
            auto* Jelly=World->SpawnActorDeferred<AMCCataclysmJelly>(AMCCataclysmJelly::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            const auto Size=Kind==EEnemy::LargeJelly?EMCCataclysmJellySize::Large:
                Kind==EEnemy::MediumJelly?EMCCataclysmJellySize::Medium:EMCCataclysmJellySize::Small;
            Jelly->ConfigureJelly(Tongue,500.f,Size);Jelly->FinishSpawning(Pose);
            Jelly->SetActorTickEnabled(false);Enemy=Jelly;
        } else {
            FMCNutEnemySettings Settings;Settings.MaxHealth=500.f;
            if(Kind==EEnemy::Summon) {
                auto* Summon=World->SpawnActorDeferred<AMCNutKamikaze>(AMCNutKamikaze::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                Summon->ConfigureKamikaze(Tongue,Cube,FVector::OneVector,Settings,20.f);
                Summon->FinishSpawning(Pose);Summon->SetActorTickEnabled(false);Enemy=Summon;
            } else {
                auto* Nut=World->SpawnActorDeferred<AMCNutEnemy>(AMCNutEnemy::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                Nut->ConfigureEnemy(Tongue,Cube,FVector::OneVector,Settings);
                Nut->FinishSpawning(Pose);Nut->SetActorTickEnabled(false);Enemy=Nut;
            }
        }
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    FVector Surface(FVector From) const
    {
        if(const auto* Nut=Cast<AMCNutEnemy>(Enemy)) return Nut->GetToolTargetPoint(From);
        if(const auto* Candy=Cast<AMCIceEvent>(Enemy)) return Candy->CandyContactPoint(From);
        if(const auto* Candy=Cast<AMCSmallMintCandy>(Enemy)) return Candy->ContactPoint(From);
        return CastChecked<AMCBossCharacter>(Enemy)->GetMeleeTargetPoint(From);
    }
    float Health() const
    {
        if(const auto* Nut=Cast<AMCNutEnemy>(Enemy)) return Nut->Health;
        if(const auto* Candy=Cast<AMCIceEvent>(Enemy)) return Candy->CandyHealth;
        if(const auto* Candy=Cast<AMCSmallMintCandy>(Enemy)) return Candy->Health;
        return CastChecked<AMCBossCharacter>(Enemy)->Runtime.Health;
    }
    float Shield() const {const auto* Knight=Cast<AMCNutBoss>(Enemy);return Knight?Knight->ShieldHealth:0.f;}
    void Place(FVector Point,FVector Facing)
    {Hero->SetActorLocationAndRotation(Point,Facing.Rotation(),false,nullptr,ETeleportType::TeleportPhysics);}
    void StandOff(float Distance,FVector Out=FVector::ForwardVector)
    {
        const FVector Point=Surface(Enemy->GetActorLocation()+Out*2000.f);
        Place(Point+Out*Distance,-Out);
    }
    void Step(float Seconds)
    {
        for(int32 I=0;I<FMath::CeilToInt(Seconds*60.f);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);}
    }
    void Swing() {Hero->SwingBrush();Step(Hero->Inventory->SwingContactTime()+.2f);}
    void Wall()
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box);Box->SetBoxExtent(FVector(5,200,150));
        Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();
        Actor->SetActorLocation(FMath::Lerp(Hero->GetActorLocation(),Surface(Hero->GetActorLocation()),.5f));
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEnemyWeaponSurfaceReach,"MessControl.Combat.EnemyWeapons.SurfaceReachAcrossFamilies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEnemyWeaponSurfaceReach::RunTest(const FString&)
{
    using namespace MCEnemyWeaponContactTests;
    for(const auto Kind:{EEnemy::Nut,EEnemy::Summon,EEnemy::LargeJelly,EEnemy::MediumJelly,EEnemy::SmallJelly,EEnemy::Knight,EEnemy::Mage,EEnemy::OtherBoss})
    for(const auto Tool:{EMCToolSlot::Knife,EMCToolSlot::Pickaxe}) {
        FWorld T(Kind,Tool);T.StandOff(140.f);
        TestTrue(TEXT("The fixture is inside weapon reach but beyond default care reach"),
            FVector::Distance(T.Hero->GetActorLocation(),T.Surface(T.Hero->GetActorLocation()))>T.Hero->Status->Settings.Reach);
        const float Before=T.Health(),ShieldBefore=T.Shield(),Damage=T.Hero->Inventory->Damage();
        T.Swing();
        TestEqual(*FString::Printf(TEXT("Enemy %d tool %d confirms the ordinary swing"),int32(Kind),int32(Tool)),T.Hero->ConfirmedHitCount,1);
        if(Kind==EEnemy::Knight) {
            TestEqual(TEXT("The shield consumes the real contact before HP"),T.Health(),Before);
            TestTrue(TEXT("A shield-only hit removes one damage amount"),FMath::IsNearlyEqual(T.Shield(),ShieldBefore-Damage));
        } else TestTrue(TEXT("The visible body takes exactly one damage amount"),FMath::IsNearlyEqual(T.Health(),Before-Damage));
        const float After=T.Health(),ShieldAfter=T.Shield();T.Step(.5f);
        TestEqual(TEXT("Follow-through cannot repeat HP damage"),T.Health(),After);
        TestEqual(TEXT("Follow-through cannot repeat shield damage"),T.Shield(),ShieldAfter);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEnemyWeaponInsideVolume,"MessControl.Combat.EnemyWeapons.InsideVolumeAndFacing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEnemyWeaponInsideVolume::RunTest(const FString&)
{
    using namespace MCEnemyWeaponContactTests;
    for(const auto Kind:{EEnemy::Nut,EEnemy::Summon,EEnemy::LargeJelly,EEnemy::Knight,EEnemy::Mage,EEnemy::OtherBoss})
    for(const bool FacingEnemy:{false,true}) {
        FWorld T(Kind);
        // Teleport only sets up the overlapping edge case. It does not bypass
        // the real input, contact timer, facing gate or damage resolver.
        T.Place(T.Enemy->GetActorLocation()+FVector(20,0,0),FacingEnemy?-FVector::ForwardVector:FVector::ForwardVector);
        TestTrue(TEXT("The worker is inside the real melee volume"),T.Surface(T.Hero->GetActorLocation()).Equals(T.Hero->GetActorLocation(),.1f));
        const float Before=T.Health(),ShieldBefore=T.Shield();T.Swing();
        TestEqual(TEXT("Facing toward a body confirms; facing away still misses"),T.Hero->ConfirmedHitCount,FacingEnemy?1:0);
        if(!FacingEnemy) {
            TestEqual(TEXT("An overlapping enemy behind the worker takes no HP damage"),T.Health(),Before);
            TestEqual(TEXT("An overlapping shield behind the worker is not damaged"),T.Shield(),ShieldBefore);
        }
    }
    // Exact coincident centres have no left/right direction; a nearby body is
    // still hittable instead of rejecting every possible facing direction.
    FWorld T(EEnemy::Nut);T.Place(T.Enemy->GetActorLocation(),FVector::ForwardVector);T.Swing();
    TestEqual(TEXT("Coincident centres do not create a zero-direction blind spot"),T.Hero->ConfirmedHitCount,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCEnemyWeaponContactGuards,"MessControl.Combat.EnemyWeapons.RangeHeightAndOcclusion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCEnemyWeaponContactGuards::RunTest(const FString&)
{
    using namespace MCEnemyWeaponContactTests;
    for(const auto Kind:{EEnemy::Nut,EEnemy::Knight,EEnemy::OtherBoss})
    for(int32 Case=0;Case<5;++Case) {
        FWorld T(Kind);
        if(Case==0) {T.StandOff(140.f);T.Wall();}
        else if(Case==1) T.StandOff(T.Hero->Inventory->MeleeReach()+5.f);
        else if(Case==2) {
            T.StandOff(80.f);T.Place(T.Hero->GetActorLocation(),FVector::ForwardVector);
        } else if(Case==3) {
            const FVector Top=T.Surface(T.Enemy->GetActorLocation()+FVector(0,0,2000));
            T.Place(Top+FVector(0,0,125),FVector::ForwardVector);
        } else T.StandOff(140.f,FVector(1,0,1).GetSafeNormal());
        const float Before=T.Health(),ShieldBefore=T.Shield();T.Swing();
        TestEqual(TEXT("Only the visible, reachable diagonal body contact is accepted"),T.Hero->ConfirmedHitCount,Case==4?1:0);
        if(Case!=4) {
            TestEqual(TEXT("Wall, reach, rear and vertical rejection preserve HP"),T.Health(),Before);
            TestEqual(TEXT("Wall, reach, rear and vertical rejection preserve the shield"),T.Shield(),ShieldBefore);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCandyWeaponSurfaceContact,"MessControl.Combat.EnemyWeapons.CandySurfaceAndOverlap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCandyWeaponSurfaceContact::RunTest(const FString&)
{
    using namespace MCEnemyWeaponContactTests;
    for(const auto Kind:{EEnemy::IceCandy,EEnemy::SmallMint}) for(int32 Case=0;Case<3;++Case) {
        FWorld T(Kind,EMCToolSlot::Pickaxe,Case==0);
        const auto* Body=CastChecked<UPrimitiveComponent>(T.Enemy->GetRootComponent());
        TestEqual(TEXT("An active mint already blocks the player capsule"),Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
        if(Case==0) T.StandOff(140.f);
        else T.Place(T.Enemy->GetActorLocation()+FVector(Case==1?20.f:0.f,0,0),-FVector::ForwardVector);
        if(Case==0) {
            UStaticMesh* Mesh=CastChecked<UStaticMeshComponent>(T.Enemy->GetRootComponent())->GetStaticMesh();
            TestTrue(TEXT("The authored winter candy is tested rather than a fixture cube"),
                Mesh && Mesh->GetPathName()==TEXT("/Game/Gameplay/Cold/Candy/SM_WinterCandy.SM_WinterCandy"));
            const UBodySetup* Setup=Mesh?Mesh->GetBodySetup():nullptr;
            TestTrue(TEXT("The saved candy has simple collision for ordinary capsule movement and contacts"),Setup && Setup->AggGeom.GetElementCount()>0);
            const FVector Start=T.Hero->GetActorLocation();FHitResult Block;
            T.Hero->SetActorLocation(Body->Bounds.Origin,true,&Block);
            TestTrue(TEXT("The real player capsule sweep stops against the authored candy"),Block.bBlockingHit && Block.GetActor()==T.Enemy);
            T.Place(Start,-FVector::ForwardVector);
        }
        const float Before=T.Health(),Damage=T.Hero->Inventory->Damage();T.Swing();
        TestEqual(TEXT("Reachable and overlapping candy contacts confirm the ordinary pickaxe swing"),T.Hero->ConfirmedHitCount,1);
        TestTrue(TEXT("The active candy loses exactly one strike of health"),FMath::IsNearlyEqual(T.Health(),Before-Damage));
        const float After=T.Health();T.Step(.5f);
        TestEqual(TEXT("Pickaxe follow-through cannot damage candy twice"),T.Health(),After);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCCandyWeaponContactGuards,"MessControl.Combat.EnemyWeapons.CandyGuardsRemain",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCCandyWeaponContactGuards::RunTest(const FString&)
{
    using namespace MCEnemyWeaponContactTests;
    for(const auto Kind:{EEnemy::IceCandy,EEnemy::SmallMint}) for(int32 Case=0;Case<7;++Case) {
        FWorld T(Kind,Case==5?EMCToolSlot::Knife:EMCToolSlot::Pickaxe);T.StandOff(140.f);
        if(Case==0) T.Wall();
        else if(Case==1) T.StandOff(T.Hero->Inventory->MeleeReach()+5.f);
        else if(Case==2) T.Place(T.Hero->GetActorLocation(),FVector::ForwardVector);
        else if(Case==3) T.Place(T.Enemy->GetActorLocation()+FVector(20,0,0),FVector::ForwardVector);
        else if(Case==4) {
            if(auto* Candy=Cast<AMCIceEvent>(T.Enemy)) Candy->Stage=EMCIceEventStage::Arrival;
            else CastChecked<AMCSmallMintCandy>(T.Enemy)->Stage=EMCSmallMintStage::Falling;
        } else if(Case==6) {
            const FVector Top=T.Surface(T.Enemy->GetActorLocation()+FVector(0,0,2000));
            T.Place(Top+FVector(0,0,125),FVector::ForwardVector);
        }
        const float Before=T.Health();T.Swing();
        TestEqual(TEXT("Walls, range, rear, inactive, wrong-tool and height contacts do not confirm"),T.Hero->ConfirmedHitCount,0);
        TestEqual(TEXT("Rejected ordinary swings preserve candy health"),T.Health(),Before);
    }
    return true;
}
#endif
