#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCExpressionComponent.h"
#include "MCGameState.h"
#include "MCNutBoss.h"
#include "MCRewardChest.h"
#include "MCThroat.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace MCKnightRollFearTests
{
/** Real actors and imported player morphs; directed cosmetic evaluation only.
 * No controller, gameplay damage, boss AI ticks, viewport or profile writes. */
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    const FVector Origin=FVector(0,0,10000);
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Game=World->SpawnActor<AMCGameState>();World->SetGameState(Game);
        Game->Phase=EMCShiftPhase::Working;Game->bTutorialActive=Game->bLobbyWaiting=false;
    }
    ~FWorld()
    {World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
    AMCNutBoss* Knight(FVector Offset=FVector::ZeroVector)
    {
        const FTransform Pose(Origin+Offset);
        auto* Boss=World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose,
            nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Boss->State=EMCNutBossState::Executing;Boss->Attack=EMCNutBossAttack::Roll;
        Boss->BossRole=EMCNutBossRole::Tank;Boss->Health=100;Boss->bDefeated=false;
        Boss->FinishSpawning(Pose);Boss->SetActorTickEnabled(false);Boss->SetActorEnableCollision(false);
        return Boss;
    }
    AMCToothCharacter* Player(FVector Offset)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero=World->SpawnActor<AMCToothCharacter>(Origin+Offset,FRotator::ZeroRotator,Params);
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorTickEnabled(false);
        Hero->Status->Initialize(100);Hero->SetActorEnableCollision(false);
        return Hero;
    }
    void Face(AMCToothCharacter* Hero,bool bSpeaking=false,int32 Frames=36)
    {
        auto* Mesh=Hero->GetMesh();const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
        for(int32 Frame=0;Frame<Frames;++Frame) {
            Hero->Expression->SetSpeechInput(bSpeaking?1.f:0.f,bSpeaking?MCViseme::Open:MCViseme::Rest);
            TArray<FTransform> Pose=Ref.GetRefBonePose();
            Hero->Expression->BuildFacePose(Pose,Ref,1.f/60.f);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCKnightRollFearProximityTest,"MessControl.Animation.KnightRollFear.NearbyPlayersAndLifecycle",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCKnightRollFearProximityTest::RunTest(const FString&)
{
    MCKnightRollFearTests::FWorld F;auto* Boss=F.Knight();
    auto* Target=F.Player(FVector(400,0,0));auto* Bystander=F.Player(FVector(-400,0,0));
    auto* Edge=F.Player(FVector(575,0,0));auto* Far=F.Player(FVector(651,0,0));
    auto* Above=F.Player(FVector(0,0,651));Boss->Target=Target;
    if(!TestNotNull(TEXT("The real player mesh is loaded"),Target->GetMesh()->GetSkeletalMeshAsset())
        || !TestNotNull(TEXT("The imported Pain morph exists"),Target->GetMesh()->GetSkeletalMeshAsset()->FindMorphTarget(TEXT("Mouth_Pain")))) return false;
    TestNull(TEXT("A nearby player's visible fear does not require a local controller"),Bystander->GetController());
    TestEqual(TEXT("A nearby target receives full fear before contact"),Target->Expression->KnightRollFearAlpha(),1.f);
    TestEqual(TEXT("A nontarget behind the ball receives the same nearby fear"),Bystander->Expression->KnightRollFearAlpha(),1.f);
    TestTrue(TEXT("The 5m to 6.5m edge has a smooth half-strength midpoint"),FMath::IsNearlyEqual(Edge->Expression->KnightRollFearAlpha(),.5f,.001f));
    TestEqual(TEXT("A player beyond the proximity radius has no fear"),Far->Expression->KnightRollFearAlpha(),0.f);
    TestEqual(TEXT("A vertically distant player is excluded by three-dimensional distance"),Above->Expression->KnightRollFearAlpha(),0.f);
    const float TargetHealth=Target->Status->State.Health,BystanderHealth=Bystander->Status->State.Health;
    const double TargetDamageAt=Target->Status->State.DamageAt,BystanderDamageAt=Bystander->Status->State.DamageAt;
    F.Face(Target);F.Face(Bystander);
    TestEqual(TEXT("The unhit target actually selects the existing Pain expression"),Target->Expression->CurrentEmotion,EMCEmotion::Pain);
    TestEqual(TEXT("The unhit bystander actually selects the existing Pain expression"),Bystander->Expression->CurrentEmotion,EMCEmotion::Pain);
    TestTrue(TEXT("Both real player meshes display the authored Pain mouth"),Target->GetMesh()->GetMorphTarget(TEXT("Mouth_Pain"))>.98f
        && Bystander->GetMesh()->GetMorphTarget(TEXT("Mouth_Pain"))>.98f);
    Boss->SetActorLocation(F.Origin+FVector(2500,0,0),false,nullptr,ETeleportType::TeleportPhysics);
    F.Face(Target);F.Face(Bystander);
    TestEqual(TEXT("Moving the rolling threat away immediately releases the fear source"),Target->Expression->KnightRollFearAlpha(),0.f);
    TestEqual(TEXT("The underlying ordinary face returns after the ball leaves"),Target->Expression->CurrentEmotion,EMCEmotion::Neutral);
    TestTrue(TEXT("The imported Pain morph relaxes on both players"),Target->GetMesh()->GetMorphTarget(TEXT("Mouth_Pain"))<.01f
        && Bystander->GetMesh()->GetMorphTarget(TEXT("Mouth_Pain"))<.01f);
    TestEqual(TEXT("Fear never damages the target"),Target->Status->State.Health,TargetHealth);
    TestEqual(TEXT("Fear never damages the bystander"),Bystander->Status->State.Health,BystanderHealth);
    TestEqual(TEXT("Fear never forges a target damage timestamp"),Target->Status->State.DamageAt,TargetDamageAt);
    TestEqual(TEXT("Fear never forges a bystander damage timestamp"),Bystander->Status->State.DamageAt,BystanderDamageAt);
    Boss->SetActorLocation(F.Origin,false,nullptr,ETeleportType::TeleportPhysics);
    auto* Hero=Target;
    auto* Face=Hero->Expression.Get();
    for(const auto State:{EMCNutBossState::Telegraph,EMCNutBossState::Recovery,EMCNutBossState::Idle,
        EMCNutBossState::Stunned,EMCNutBossState::Defeated,EMCNutBossState::Transforming,EMCNutBossState::Falling}) {
        Boss->State=State;
        TestEqual(*FString::Printf(TEXT("Nonrolling state %d cannot retain the nearby face"),int32(State)),Face->KnightRollFearAlpha(),0.f);
    }
    Boss->State=EMCNutBossState::Executing;Boss->Attack=EMCNutBossAttack::Charge;
    TestEqual(TEXT("A different moving attack does not borrow roll fear"),Face->KnightRollFearAlpha(),0.f);
    Boss->Attack=EMCNutBossAttack::Roll;Boss->BossRole=EMCNutBossRole::Mage;
    TestEqual(TEXT("The mage never becomes a rolling knight fear source"),Face->KnightRollFearAlpha(),0.f);
    Boss->BossRole=EMCNutBossRole::Tank;Boss->bDefeated=true;
    TestEqual(TEXT("A defeated knight cannot leave fear active"),Face->KnightRollFearAlpha(),0.f);
    Boss->bDefeated=false;Boss->Health=0;
    TestEqual(TEXT("A zero-health knight is excluded even before its state changes"),Face->KnightRollFearAlpha(),0.f);
    Boss->Health=100;
    Hero->Status->State.Health=0;
    TestEqual(TEXT("Dead players do not receive proximity fear"),Face->KnightRollFearAlpha(),0.f);
    Hero->Status->State.Health=100;
    auto* Throat=F.World->SpawnActor<AMCThroat>(F.Origin+FVector(5000,0,0),FRotator::ZeroRotator);
    Throat->SetActorTickEnabled(false);Hero->SwallowedBy=Throat;
    TestEqual(TEXT("Swallowed players do not receive proximity fear"),Face->KnightRollFearAlpha(),0.f);
    Hero->SwallowedBy=nullptr;
    auto* Mimic=F.World->SpawnActor<AMCRewardChest>(F.Origin+FVector(-5000,0,0),FRotator::ZeroRotator);
    Mimic->SetActorTickEnabled(false);Hero->MimicCaptor=Mimic;
    TestEqual(TEXT("Mimic-contained players do not receive proximity fear"),Face->KnightRollFearAlpha(),0.f);
    Hero->MimicCaptor=nullptr;
    auto* Other=F.Knight(FVector(0,100,0));Boss->State=EMCNutBossState::Recovery;
    TestEqual(TEXT("Stopping one knight does not cancel another nearby knight"),Face->KnightRollFearAlpha(),1.f);
    Other->BossRole=EMCNutBossRole::Mage;
    TestEqual(TEXT("Changing the remaining knight to mage removes fear without a stale lease"),Face->KnightRollFearAlpha(),0.f);
    Other->BossRole=EMCNutBossRole::Tank;Other->Health=0;
    TestEqual(TEXT("Death of the remaining source removes fear"),Face->KnightRollFearAlpha(),0.f);
    Other->Health=100;Other->Destroy();
    TestEqual(TEXT("Destroying the source also releases fear"),Face->KnightRollFearAlpha(),0.f);
    Hero->Destroy();
    TestEqual(TEXT("A destroyed player safely reports no fear"),Face->KnightRollFearAlpha(),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCKnightRollFearFacePriorityTest,"MessControl.Animation.KnightRollFear.PainOverridesEmoteAndSpeechThenRestores",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCKnightRollFearFacePriorityTest::RunTest(const FString&)
{
    MCKnightRollFearTests::FWorld F;auto* Boss=F.Knight(FVector(2500,0,0));auto* Hero=F.Player(FVector(400,0,0));
    auto* Face=Hero->Expression.Get();auto* Mesh=Hero->GetMesh();
    if(!TestNotNull(TEXT("The player has the saved emote library"),Face->Library.Get())
        || !TestNotNull(TEXT("The player has its imported skeletal mesh"),Mesh->GetSkeletalMeshAsset())) return false;
    const auto* Love=Face->Library->Entries.FindByPredicate([](const FMCEmoteEntry& Entry) {return Entry.Id==TEXT("love");});
    if(!TestNotNull(TEXT("The existing eye-only love emote is available"),Love)
        || !TestNotNull(TEXT("The artist eye morph is available"),Mesh->GetSkeletalMeshAsset()->FindMorphTarget(Love->EyeMorph))
        || !TestNotNull(TEXT("The artist Pain mouth is available"),Mesh->GetSkeletalMeshAsset()->FindMorphTarget(TEXT("Mouth_Pain")))) return false;
    Face->ServerPlayEmote(TEXT("love"));
    if(!TestEqual(TEXT("The real emote API accepts the facial expression"),Face->State.Id,FName(TEXT("love")))) return false;
    // Settle the accepted emote envelope without ticking unrelated gameplay.
    Face->State.StartedAt-=.4;
    F.Face(Hero,true);
    TestTrue(TEXT("Before the roll, the real mesh shows the selected eye morph and live speech"),
        Mesh->GetMorphTarget(Love->EyeMorph)>.98f && Mesh->GetMorphTarget(TEXT("Mouth_A"))>.98f);
    const FMCEmoteState EmoteBefore=Face->State;
    const float HealthBefore=Hero->Status->State.Health;const double DamageBefore=Hero->Status->State.DamageAt;
    Boss->SetActorLocation(F.Origin,false,nullptr,ETeleportType::TeleportPhysics);
    F.Face(Hero,true);
    TestEqual(TEXT("Nearby roll fear selects Pain even during an explicit emote"),Face->CurrentEmotion,EMCEmotion::Pain);
    TestTrue(TEXT("Pain replaces both eye-only emote and voice instead of losing the artist mouth"),
        Mesh->GetMorphTarget(TEXT("Mouth_Pain"))>.98f && Mesh->GetMorphTarget(TEXT("Mouth_A"))<.01f
        && Mesh->GetMorphTarget(Love->EyeMorph)<.01f);
    TestEqual(TEXT("The emote identity is preserved"),Face->State.Id,EmoteBefore.Id);
    TestEqual(TEXT("The emote serial is preserved"),Face->State.Serial,EmoteBefore.Serial);
    TestEqual(TEXT("The emote clock is preserved"),Face->State.StartedAt,EmoteBefore.StartedAt);
    TestEqual(TEXT("Fear does not cancel the emote"),Face->State.StoppedAt,EmoteBefore.StoppedAt);
    Boss->State=EMCNutBossState::Recovery;F.Face(Hero,true);
    TestEqual(TEXT("The selected expression returns after the ball stops"),Face->CurrentEmotion,Love->Emotion);
    TestTrue(TEXT("The actual eye morph and live speech return after fear ends"),Mesh->GetMorphTarget(Love->EyeMorph)>.98f
        && Mesh->GetMorphTarget(TEXT("Mouth_A"))>.98f && Mesh->GetMorphTarget(TEXT("Mouth_Pain"))<.01f);
    Face->State.Id=NAME_None;F.Face(Hero);
    TestEqual(TEXT("Ending the ordinary emote returns to the neutral face"),Face->CurrentEmotion,EMCEmotion::Neutral);
    TestTrue(TEXT("The Pain mouth remains released after the ordinary emote ends"),Mesh->GetMorphTarget(TEXT("Mouth_Pain"))<.01f);
    TestEqual(TEXT("Face-priority evaluation never changes health"),Hero->Status->State.Health,HealthBefore);
    TestEqual(TEXT("Face-priority evaluation never records gameplay pain"),Hero->Status->State.DamageAt,DamageBefore);
    return true;
}
#endif
