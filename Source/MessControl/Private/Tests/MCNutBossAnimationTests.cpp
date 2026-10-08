#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCNutBoss.h"
#include "MCNutBossAnimInstance.h"
#include "MCTongue.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "ReferenceSkeleton.h"
#include "UObject/UnrealType.h"

namespace MCNutBossAnimationTestsPrivate
{
struct FFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    USkeleton* Skeleton=NewObject<USkeleton>();
    USkeletalMesh* Mesh=NewObject<USkeletalMesh>();
    AMCNutBoss* Boss=nullptr;
    AMCTongue* Tongue=nullptr;
    FMCNutBossSettings Settings;
    UAnimSequence *Idle=nullptr,*Walk=nullptr,*Cast=nullptr,*Tell=nullptr,*Loop=nullptr,*Recovery=nullptr,*Melee=nullptr;

    UAnimSequence* Clip(int32 Frames,int32 Fps,USkeleton* OnSkeleton=nullptr)
    {
        USkeleton* ClipSkeleton=OnSkeleton?OnSkeleton:Skeleton;
        // A separate skeleton tests asset compatibility; it still needs a valid compression reference pose.
        if(ClipSkeleton->GetReferenceSkeleton().GetNum()==0) {
            FReferenceSkeletonModifier Reference(ClipSkeleton);
            Reference.Add(FMeshBoneInfo(TEXT("root"),TEXT("root"),INDEX_NONE),FTransform::Identity);
        }
        auto* Result=NewObject<UAnimSequence>();
        Result->SetSkeleton(ClipSkeleton);
        // UE 5.8 keeps the compression target rate separate from the data-model rate.
        // Match both before model notifications: the authored 8/25-second loop cannot be resampled to 30 fps.
        auto* TargetRate=FindFProperty<FStructProperty>(UAnimSequence::StaticClass(),TEXT("PlatformTargetFrameRate"));
        check(TargetRate);
        TargetRate->ContainerPtrToValuePtr<FPerPlatformFrameRate>(Result)->Default=FFrameRate(Fps,1);
        auto& Controller=Result->GetController();
        Controller.InitializeModel();
        Controller.SetFrameRate(FFrameRate(Fps,1),false);
        Controller.SetNumberOfFrames(FFrameNumber(Frames),false);
        return Result;
    }

    FFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        {
            FReferenceSkeletonModifier Reference(Skeleton);
            Reference.Add(FMeshBoneInfo(TEXT("root"),TEXT("root"),INDEX_NONE),FTransform::Identity);
        }
        Mesh->SetSkeleton(Skeleton); Mesh->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
        Idle=Clip(25,25); Walk=Clip(50,25); Cast=Clip(60,30);
        Tell=Clip(27,25); Loop=Clip(8,25); Recovery=Clip(35,25); Melee=Clip(100,30);
        // No saved character, animation or encounter assets are loaded by this fixture.
        Settings.WholeMesh=Settings.TankBallMesh=Settings.ShellMesh=Settings.KernelMesh=nullptr;
        Settings.TankSkeletalMesh=Settings.MageSkeletalMesh=Mesh;
        Settings.TankIdleAnimation=Settings.TankMeleeAnimation=Settings.TankJumpAnimation=Settings.TankTransformAnimation=Idle;
        Settings.TankWalkAnimation=Settings.TankWalkLeftAnimation=Settings.TankWalkRightAnimation=Walk;
        Settings.MageIdleAnimation=Settings.MageHeavyCastAnimation=Settings.MageSummonAnimation=Settings.MageRainAnimation=Idle;
        Settings.MageHitAnimation=Settings.MageDeathAnimation=Idle; Settings.MageWalkAnimation=Walk; Settings.MageCastAnimation=Cast;
        Settings.TankChargeTellAnimation=Tell; Settings.TankChargeLoopAnimation=Loop; Settings.TankChargeRecoveryAnimation=Recovery;
        Settings.MageMeleeAnimation=Melee;
        // Deferred actors stay out of BeginPlay and gameplay ticking: only the animation snapshot is exercised.
        const FTransform Pose=FTransform::Identity;
        Tongue=World->SpawnActorDeferred<AMCTongue>(AMCTongue::StaticClass(),Pose);
        Boss=World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose);
        Configure(EMCNutBossRole::Tank);
    }

    ~FFixture()
    {
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    }

    void Configure(EMCNutBossRole Role)
    {
        Boss->ConfigureEncounter(Tongue,nullptr,Role,Settings,1,41);
    }

    void Phase(EMCNutBossAttack Attack,EMCNutBossState State,double Age,double Duration)
    {
        Boss->Attack=Attack; Boss->State=State;
        Boss->StateStartedAt=World->GetTimeSeconds()-Age;
        Boss->AttackEndAt=Boss->StateStartedAt+Duration;
        Boss->ResolveAt=State==EMCNutBossState::Telegraph?Boss->AttackEndAt:Boss->StateStartedAt;
    }

    FMCNutBossAnimationSnapshot Snapshot() const
    {
        FMCNutBossAnimationSnapshot Result;
        Boss->BuildAnimationSnapshot(Boss->BossRole==EMCNutBossRole::Mage?Boss->MageModel.Get():Boss->TankModel.Get(),Result);
        return Result;
    }
};

const FMCNutBossClipSample* Find(const FMCNutBossAnimationSnapshot& Snapshot,const UAnimSequence* Clip)
{
    return Snapshot.Samples.FindByPredicate([&](const auto& Sample){return Sample.Clip==Clip;});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossChargeAnimationTest,"MessControl.CoreLoop.NutBoss.Animation.ChargePhasesAndServerClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossChargeAnimationTest::RunTest(const FString&)
{
    using namespace MCNutBossAnimationTestsPrivate;
    FFixture T;
    T.Phase(EMCNutBossAttack::Charge,EMCNutBossState::Telegraph,.55,1.1);
    auto Snapshot=T.Snapshot(); auto* Sample=Find(Snapshot,T.Tell);
    if(!TestNotNull(TEXT("Halfway through the warning selects the dedicated tell"),Sample)) return false;
    TestFalse(TEXT("The warning plays once"),Sample->bLoop);
    TestEqual(TEXT("Half the warning reaches half the authored tell"),Sample->Seconds,.54f,.001f);
    TestNull(TEXT("A dedicated warning does not accelerate ordinary walk"),Find(Snapshot,T.Walk));

    T.Phase(EMCNutBossAttack::Charge,EMCNutBossState::Executing,0,1.7);
    Snapshot=T.Snapshot(); Sample=Find(Snapshot,T.Loop);
    if(!TestNotNull(TEXT("Execution selects the charge loop"),Sample)) return false;
    TestTrue(TEXT("Only the running phase repeats"),Sample->bLoop);
    TestEqual(TEXT("A new charge starts at the first contact pose"),Sample->Seconds,0.f);
    T.Phase(EMCNutBossAttack::Charge,EMCNutBossState::Executing,.04,1.7);
    Snapshot=T.Snapshot(); Sample=Find(Snapshot,T.Loop);
    if(!TestNotNull(TEXT("The charge loop remains selected"),Sample)) return false;
    TestEqual(TEXT("Forty server milliseconds advance eighty clip milliseconds"),Sample->Seconds,.08f,.001f);
    T.Phase(EMCNutBossAttack::Charge,EMCNutBossState::Executing,.19,1.7);
    Snapshot=T.Snapshot(); Sample=Find(Snapshot,T.Loop);
    if(!TestNotNull(TEXT("The run continues after one full cycle"),Sample)) return false;
    TestEqual(TEXT("The 0.32 second clip wraps at the configured rate"),Sample->Seconds,.06f,.001f);
    T.Phase(EMCNutBossAttack::Charge,EMCNutBossState::Executing,0,1.7);
    Snapshot=T.Snapshot(); Sample=Find(Snapshot,T.Loop);
    if(!TestNotNull(TEXT("A subsequent charge keeps its run clip"),Sample)) return false;
    TestEqual(TEXT("Each new execution resets its own phase"),Sample->Seconds,0.f);

    T.Phase(EMCNutBossAttack::Charge,EMCNutBossState::Recovery,.7,1.4);
    Snapshot=T.Snapshot(); Sample=Find(Snapshot,T.Recovery);
    if(!TestNotNull(TEXT("Braking selects the dedicated recovery"),Sample)) return false;
    TestFalse(TEXT("Braking does not repeat"),Sample->bLoop);
    TestEqual(TEXT("Recovery follows the complete server interval"),Sample->Seconds,.7f,.001f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossChargeFallbackTest,"MessControl.CoreLoop.NutBoss.Animation.OptionalChargeFallback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossChargeFallbackTest::RunTest(const FString&)
{
    using namespace MCNutBossAnimationTestsPrivate;
    FFixture T;
    T.Settings.TankChargeTellAnimation=T.Settings.TankChargeLoopAnimation=T.Settings.TankChargeRecoveryAnimation=nullptr;
    T.Configure(EMCNutBossRole::Tank);
    const EMCNutBossState States[]={EMCNutBossState::Telegraph,EMCNutBossState::Executing,EMCNutBossState::Recovery};
    for(const auto State:States) {
        T.Phase(EMCNutBossAttack::Charge,State,.5,1.1);
        const auto Snapshot=T.Snapshot(); const auto* Sample=Find(Snapshot,T.Walk);
        if(!TestNotNull(TEXT("An older profile retains walk through every charge phase"),Sample)) return false;
        TestTrue(TEXT("The legacy charge walk remains looping"),Sample->bLoop);
    }
    T.Settings.TankChargeTellAnimation=T.Tell; T.Settings.TankChargeRecoveryAnimation=T.Recovery;
    T.Configure(EMCNutBossRole::Tank);
    T.Phase(EMCNutBossAttack::Charge,EMCNutBossState::Telegraph,.55,1.1);
    TestNotNull(TEXT("A partial profile uses its available warning"),Find(T.Snapshot(),T.Tell));
    T.Phase(EMCNutBossAttack::Charge,EMCNutBossState::Executing,.04,1.7);
    TestNotNull(TEXT("Only the missing run phase falls back to walk"),Find(T.Snapshot(),T.Walk));
    T.Phase(EMCNutBossAttack::Charge,EMCNutBossState::Recovery,.7,1.4);
    TestNotNull(TEXT("An available recovery does not require a run asset"),Find(T.Snapshot(),T.Recovery));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossMageMeleeAnimationTest,"MessControl.CoreLoop.NutBoss.Animation.MageMeleePreservesShove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossMageMeleeAnimationTest::RunTest(const FString&)
{
    using namespace MCNutBossAnimationTestsPrivate;
    FFixture T; T.Configure(EMCNutBossRole::Mage);
    T.Settings.MageCastReleaseFraction=.7f; T.Configure(EMCNutBossRole::Mage);
    T.Phase(EMCNutBossAttack::Melee,EMCNutBossState::Telegraph,.325,.65);
    auto Snapshot=T.Snapshot(); auto* Sample=Find(Snapshot,T.Melee);
    if(!TestNotNull(TEXT("The mage selects its independent close-range clip"),Sample)) return false;
    TestFalse(TEXT("The palm shove plays once"),Sample->bLoop);
    TestEqual(TEXT("Melee retains the authored 55 percent contact independently of spell tuning"),Sample->Seconds/T.Melee->GetPlayLength(),.275f,.001f);
    TestEqual(TEXT("Caster aim and palm IK receive no weight during the shove"),Snapshot.Cast,0.f);
    T.Phase(EMCNutBossAttack::Melee,EMCNutBossState::Recovery,.05,.9);
    Snapshot=T.Snapshot(); Sample=Find(Snapshot,T.Melee);
    if(!TestNotNull(TEXT("The dedicated shove continues into recovery"),Sample)) return false;
    TestEqual(TEXT("Spell-hand IK stays disabled during melee recovery"),Snapshot.Cast,0.f);
    TestEqual(TEXT("A spell release recoil cannot overwrite the contact pose"),Snapshot.Release,0.f);
    TestEqual(TEXT("Melee does not enter the rain channel"),Snapshot.Channel,0.f);

    T.Phase(EMCNutBossAttack::Fireball,EMCNutBossState::Recovery,.05,.9);
    Snapshot=T.Snapshot();
    TestNotNull(TEXT("The fireball still uses its spell clip"),Find(Snapshot,T.Cast));
    TestTrue(TEXT("Fireball retains casting aim and palm IK weight"),Snapshot.Cast>.9f);
    TestTrue(TEXT("Fireball retains its release pulse"),Snapshot.Release>0);
    T.Settings.MageMeleeAnimation=nullptr; T.Configure(EMCNutBossRole::Mage);
    T.Phase(EMCNutBossAttack::Melee,EMCNutBossState::Recovery,.05,.9);
    Snapshot=T.Snapshot();
    TestNotNull(TEXT("Older mage profiles retain the cast fallback"),Find(Snapshot,T.Cast));
    TestTrue(TEXT("The old cast fallback preserves its existing overlays"),Snapshot.Cast>.9f);
    T.Settings.MageMeleeAnimation=T.Clip(100,30,NewObject<USkeleton>()); T.Configure(EMCNutBossRole::Mage);
    T.Phase(EMCNutBossAttack::Melee,EMCNutBossState::Recovery,.05,.9);
    TestNotNull(TEXT("An incompatible melee skeleton falls back to cast"),Find(T.Snapshot(),T.Cast));
    return true;
}
#endif
