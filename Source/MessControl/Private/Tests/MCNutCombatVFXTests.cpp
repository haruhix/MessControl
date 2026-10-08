#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCNutCombatEffect.h"
#include "MCNutSpellProjectile.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCTongue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include <limits>
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

namespace MCNutCombatVFXTestsPrivate
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AMCTongue* Tongue=nullptr;
    AActor* Source=nullptr;
    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
#if WITH_EDITOR
        FStaticMeshCompilingManager::Get().FinishCompilation({Cube});
#endif
        const FTransform Pose(FRotator::ZeroRotator,FVector::ZeroVector,FVector(24,16,.2));
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Tongue->SourceMesh=Cube;Tongue->bAutomaticYawns=false;Tongue->FinishSpawning(Pose);Tongue->SetActorTickEnabled(false);
        Source=World->SpawnActor<AActor>();
    }
    ~FFixture() {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    void Step(float Seconds) {for(int32 I=0;I<FMath::CeilToInt(Seconds*20);++I) {++GFrameCounter;World->Tick(LEVELTICK_All,.05f);}}
    AMCToothCharacter* Hero()
    {
        auto* Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,150),FRotator::ZeroRotator);
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorTickEnabled(false);return Hero;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutCueSafetyTest,"MessControl.CoreLoop.NutVFX.DeterministicRainAndCosmeticSafety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutCueSafetyTest::RunTest(const FString&)
{
    const FVector Center(47,-81,10);
    for(int32 I=0;I<48;++I) {
        const FVector Point=AMCNutCombatEffect::GetNutRainDropPoint(Center,300,72,I);
        TestEqual(TEXT("Server and rendering peer reproduce the same indexed drop"),Point,AMCNutCombatEffect::GetNutRainDropPoint(Center,300,72,I));
        TestTrue(TEXT("Every deterministic point fits inside its warning area"),FVector::Dist2D(Point,Center)<=300 && Point.Z==Center.Z);
    }
    TestTrue(TEXT("Seed changes the rain pattern"),AMCNutCombatEffect::GetNutRainDropPoint(Center,300,72,1)!=AMCNutCombatEffect::GetNutRainDropPoint(Center,300,73,1));
    MCNutCombatVFXTestsPrivate::FFixture T;auto* Hero=T.Hero();const float Before=Hero->Status->State.Health;
    auto* Cue=AMCNutCombatEffect::Spawn(T.Source,T.Tongue,EMCNutCombatCue::NutRain,FVector::ZeroVector,FVector::ZeroVector,
        std::numeric_limits<float>::quiet_NaN(),0,1,72,90,MAX_int32,.001f);
    if(!TestNotNull(TEXT("A cosmetic rain cue is valid independently of gameplay damage"),Cue)) return false;
    TestEqual(TEXT("Corrupt radius is repaired before replication"),Cue->Cue.Radius,100.f);
    TestEqual(TEXT("Replicated rain detail count has a fixed rendering budget"),Cue->Cue.DropCount,AMCNutCombatEffect::MaxRainDrops);
    TestTrue(TEXT("Tiny cadence is bounded"),Cue->Cue.DropCadence>=.12f);
    T.Step(.7f);
    TestEqual(TEXT("Cosmetic impacts never damage a tooth standing in the rain"),Hero->Status->State.Health,Before);
    T.Source->Destroy();T.Step(.1f);
    TestTrue(TEXT("Cancelling the source cleans up an ongoing visual cue"),!IsValid(Cue) || Cue->IsActorBeingDestroyed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutFireballSweepTest,"MessControl.CoreLoop.NutVFX.ServerSweepHitsOnceAndCancels",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutFireballSweepTest::RunTest(const FString&)
{
    MCNutCombatVFXTestsPrivate::FFixture T;auto* Hero=T.Hero();const float Before=Hero->Status->State.Health;
    auto* Shot=AMCNutSpellProjectile::Spawn(T.Source,T.Tongue,FVector(-500,0,150),FVector(500,0,150),1000,18,28);
    if(!TestNotNull(TEXT("A finite locked fireball can launch"),Shot)) return false;
    Shot->SetActorTickEnabled(false);T.Step(.9f);Shot->Tick(.9f);
    TestTrue(TEXT("Sweeping accumulated travel catches the real tooth capsule at low update rate"),Shot->Impact.bImpacted);
    TestEqual(TEXT("A fireball applies one bounded server damage contact"),Shot->DamageApplications,1);
    TestEqual(TEXT("The contact uses the configured damage"),Hero->Status->State.Health,Before-18);
    Shot->Tick(.9f);
    TestEqual(TEXT("Repeated terminal updates cannot damage again"),Shot->DamageApplications,1);
    TestEqual(TEXT("Terminal damage does not repeat"),Hero->Status->State.Health,Before-18);
    TestNull(TEXT("NaN launch speed cannot spawn a hazard"),AMCNutSpellProjectile::Spawn(T.Source,T.Tongue,FVector(-500,0,150),FVector(500,0,150),std::numeric_limits<float>::quiet_NaN(),18,28));
    auto* Cancelled=AMCNutSpellProjectile::Spawn(T.Source,T.Tongue,FVector(-500,300,150),FVector(500,300,150),550,18,28);
    if(!TestNotNull(TEXT("Another spell can be owned by the encounter"),Cancelled)) return false;
    Cancelled->Cancel();T.Step(.1f);
    TestTrue(TEXT("Explicit encounter cancellation removes its travelling spell"),!IsValid(Cancelled) || Cancelled->IsActorBeingDestroyed());
    return true;
}
#endif
