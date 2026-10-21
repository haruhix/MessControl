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

    void Curve(UAnimSequence* Clip,FName Name,float Value)
    {
        const FAnimationCurveIdentifier Id(Name,ERawCurveTrackTypes::RCT_Float);
        auto& Controller=Clip->GetController();
        Controller.AddCurve(Id,AACF_DefaultCurve,false);
        Controller.SetCurveKeys(Id,{FRichCurveKey(0,Value),FRichCurveKey(Clip->GetPlayLength(),Value)},false);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutWizardArtistClipsTest,"MessControl.CoreLoop.NutBoss.Animation.WizardArtistFullClips",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutWizardArtistClipsTest::RunTest(const FString&)
{
    using namespace MCNutBossAnimationTestsPrivate;
    FFixture T;
    T.Curve(T.Cast,TEXT("MCSpellGesture"),1);T.Curve(T.Cast,TEXT("MCAuthoredWizard"),1);
    auto* Hit=T.Clip(14,30);auto* Death=T.Clip(99,30);
    T.Settings.MageHitAnimation=Hit;T.Settings.MageDeathAnimation=Death;
    T.Settings.MageTeleportAnimation=T.Cast;T.Settings.MageTeleportAnimationSeconds=T.Cast->GetPlayLength();
    T.Settings.AnimationBlendSeconds=.25f;T.Settings.MageHitAnimationSeconds=Hit->GetPlayLength();
    T.Curve(T.Idle,TEXT("MCAuthoredWizard"),1);T.Configure(EMCNutBossRole::Mage);
    const float Release=T.Settings.MageCastReleaseFraction,Exit=T.Cast->GetPlayLength()*(1-Release);
    T.Phase(EMCNutBossAttack::Teleport,EMCNutBossState::Recovery,Exit-.001,Exit+.25);
    auto Snapshot=T.Snapshot();const auto* Sample=Find(Snapshot,T.Cast);
    if(!TestNotNull(TEXT("Teleport selects its dedicated authored clip"),Sample)) return false;
    TestTrue(TEXT("The final source frame retains full weight before blend-out"),Sample->Seconds>T.Cast->GetPlayLength()-.002f && FMath::IsNearlyEqual(Sample->Weight,1.f));
    TestFalse(TEXT("Teleport never loops the source"),Sample->bLoop);
    TestEqual(TEXT("All transitions use the requested quarter second"),Snapshot.BlendSeconds,.25f);
    TestEqual(TEXT("Authored wizard pose suppresses procedural replacement"),Snapshot.AuthoredMageMotionWeight,1.f);
    T.Phase(EMCNutBossAttack::Teleport,EMCNutBossState::Recovery,Exit+.125,Exit+.25);
    Snapshot=T.Snapshot();Sample=Find(Snapshot,T.Cast);
    if(!TestNotNull(TEXT("The completed teleport holds its final pose during blend-out"),Sample)) return false;
    TestEqual(TEXT("Blend-out holds the final frame, not a truncated earlier pose"),Sample->Seconds,T.Cast->GetPlayLength(),.001f);
    T.Phase(EMCNutBossAttack::None,EMCNutBossState::Idle,.5,0);
    T.Boss->VisualHitAt=T.World->GetTimeSeconds()-T.Settings.MageHitAnimationSeconds;
    Snapshot=T.Snapshot();Sample=Find(Snapshot,Hit);
    if(!TestNotNull(TEXT("The full damage reaction reaches its last frame"),Sample)) return false;
    TestEqual(TEXT("Damage reaction completes before fading"),Sample->Seconds,Hit->GetPlayLength(),.001f);
    T.Boss->bDefeated=true;T.Phase(EMCNutBossAttack::None,EMCNutBossState::Defeated,Death->GetPlayLength(),Death->GetPlayLength()+.25);
    Snapshot=T.Snapshot();Sample=Find(Snapshot,Death);
    if(!TestNotNull(TEXT("Death keeps its final authored pose before despawn"),Sample)) return false;
    TestEqual(TEXT("Death plays the complete source length"),Sample->Seconds,Death->GetPlayLength(),.001f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutWizardRainIdleTest,"MessControl.CoreLoop.NutBoss.Animation.WizardRainReturnsToLiveIdle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutWizardRainIdleTest::RunTest(const FString&)
{
    using namespace MCNutBossAnimationTestsPrivate;
    FFixture T;auto* Rain=T.Clip(98,30);
    T.Curve(Rain,TEXT("MCSpellGesture"),1);T.Curve(Rain,TEXT("MCAuthoredWizard"),1);
    T.Curve(T.Idle,TEXT("MCAuthoredWizard"),1);
    T.Settings.MageRainAnimation=Rain;T.Settings.MageRainAnimationSeconds=Rain->GetPlayLength();
    T.Settings.RainWindup=Rain->GetPlayLength()*.55f;T.Settings.AnimationBlendSeconds=.25f;
    T.Configure(EMCNutBossRole::Mage);
    const float Full=Rain->GetPlayLength()-T.Settings.RainWindup;
    T.Phase(EMCNutBossAttack::NutRain,EMCNutBossState::Executing,Full-.001,4.);
    auto Snapshot=T.Snapshot();const auto* Sample=Find(Snapshot,Rain);
    if(!TestNotNull(TEXT("Rain reaches its complete final source frame before blending"),Sample)) return false;
    TestEqual(TEXT("The source plays forward instead of holding its release pose"),Sample->Seconds,Rain->GetPlayLength()-.001f,.001f);
    TestEqual(TEXT("The final source frames remain fully weighted"),Sample->Weight,1.f,.001f);
    T.Phase(EMCNutBossAttack::NutRain,EMCNutBossState::Executing,Full+.125,4.);
    Snapshot=T.Snapshot();Sample=Find(Snapshot,Rain);
    if(!TestNotNull(TEXT("Rain blends out over the requested quarter second"),Sample)) return false;
    TestEqual(TEXT("The completed cast is halfway through its idle blend"),Sample->Weight,.5f,.001f);
    T.Phase(EMCNutBossAttack::NutRain,EMCNutBossState::Executing,Full+.26,4.);
    Snapshot=T.Snapshot();Sample=Find(Snapshot,T.Idle);
    TestNull(TEXT("The active storm no longer keeps a frozen rain clip"),Find(Snapshot,Rain));
    if(!TestNotNull(TEXT("The storm continues above a fully weighted live idle"),Sample)) return false;
    TestTrue(TEXT("The idle remains looping while nuts fall"),Sample->bLoop && Sample->Weight>.999f);
    TestEqual(TEXT("A channel overlay cannot recreate the old frozen casting pose"),Snapshot.Channel,0.f);
    return true;
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
    TestNotNull(TEXT("Older mage profiles retain idle beneath the procedural shove"),Find(Snapshot,T.Idle));
    TestEqual(TEXT("The procedural shove keeps spell-palm IK disabled"),Snapshot.Cast,0.f);
    TestTrue(TEXT("The missing authored clip retains the melee gesture"),Snapshot.Melee>.9f);
    T.Settings.MageMeleeAnimation=T.Clip(100,30,NewObject<USkeleton>()); T.Configure(EMCNutBossRole::Mage);
    T.Phase(EMCNutBossAttack::Melee,EMCNutBossState::Recovery,.05,.9);
    TestNotNull(TEXT("An incompatible melee skeleton retains the procedural idle fallback"),Find(T.Snapshot(),T.Idle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossMageCastGestureTest,"MessControl.CoreLoop.NutBoss.Animation.MageCastGesturesAndPalmContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossMageCastGestureTest::RunTest(const FString&)
{
    using namespace MCNutBossAnimationTestsPrivate;
    FFixture T; T.Configure(EMCNutBossRole::Mage);
    // The authored cast windup replaced the prototype's fixed .9 seconds.
    // Sample its actual midpoint, rather than halfway through an obsolete tell.
    const double Windup=T.Boss->BossSettings.FireballWindup;
    T.Phase(EMCNutBossAttack::Fireball,EMCNutBossState::Telegraph,Windup*.5,Windup);
    auto Snapshot=T.Snapshot();
    TestEqual(TEXT("Gathering uses the authored clip phase"),Snapshot.CastProgress,.275f,.001f);
    TestEqual(TEXT("The gathering hand stays free of emitter IK"),Snapshot.CastHandAlignmentWeight(),0.f);

    T.Phase(EMCNutBossAttack::Fireball,EMCNutBossState::Recovery,0,.9);
    Snapshot=T.Snapshot();
    TestEqual(TEXT("The release frame retains exact palm alignment"),Snapshot.CastHandAlignmentWeight(),.85f,.001f);
    T.Phase(EMCNutBossAttack::Fireball,EMCNutBossState::Recovery,.54,.9);
    Snapshot=T.Snapshot();
    TestEqual(TEXT("The follow-through hand returns to the authored performance"),Snapshot.CastHandAlignmentWeight(),0.f);

    for(const double Age:{0.,.5,2.,3.9}) {
        T.Phase(EMCNutBossAttack::NutRain,EMCNutBossState::Executing,Age,4.);
        Snapshot=T.Snapshot();
        TestEqual(TEXT("The entire rain channel retains palm contact"),Snapshot.CastHandAlignmentWeight(),.85f,.001f);
    }
    T.Settings.MageCastReleaseFraction=.7f; T.Configure(EMCNutBossRole::Mage);
    T.Phase(EMCNutBossAttack::Fireball,EMCNutBossState::Recovery,0,.9);
    TestEqual(TEXT("A tuned release moves the palm-contact window with the impact"),T.Snapshot().CastHandAlignmentWeight(),.85f,.001f);
    T.Settings.MageCastReleaseFraction=.9f; T.Configure(EMCNutBossRole::Mage);
    for(const double Age:{0.,.5,2.,3.9}) {
        T.Phase(EMCNutBossAttack::NutRain,EMCNutBossState::Executing,Age,4.);
        TestEqual(TEXT("A late tuned release cannot fade palm contact during the rain channel"),T.Snapshot().CastHandAlignmentWeight(),.85f,.001f);
    }
    T.Phase(EMCNutBossAttack::Teleport,EMCNutBossState::Telegraph,.325,.65);
    TestEqual(TEXT("Teleport gestures remain free of projectile palm IK"),T.Snapshot().CastHandAlignmentWeight(),0.f);
    T.Phase(EMCNutBossAttack::Melee,EMCNutBossState::Recovery,0,.9);
    TestEqual(TEXT("The melee shove remains free of projectile palm IK"),T.Snapshot().CastHandAlignmentWeight(),0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNutBossAuthoredSpellTest,"MessControl.CoreLoop.NutBoss.Animation.AuthoredSpellGesturesAndChannel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNutBossAuthoredSpellTest::RunTest(const FString&)
{
    using namespace MCNutBossAnimationTestsPrivate;
    FFixture T;
    T.Curve(T.Cast,TEXT("MCSpellGesture"),1);
    auto* Rain=T.Clip(200,30);
    T.Curve(Rain,TEXT("MCSpellGesture"),1);
    T.Curve(Rain,TEXT("MCSpellChannelEnd"),.85f);
    T.Curve(Rain,TEXT("MCSpellChannelPeriod"),1.1f);
    T.Settings.MageRainAnimation=Rain;
    T.Configure(EMCNutBossRole::Mage);
    T.Phase(EMCNutBossAttack::Fireball,EMCNutBossState::Telegraph,.89,.9);
    auto Snapshot=T.Snapshot();
    const int32 CastKey=Snapshot.VisualKey;
    TestTrue(TEXT("A marked cast retains the coordinated authored palm poses"),Snapshot.bAuthoredSpellGestures);
    TestEqual(TEXT("Fixed emitter IK cannot flatten either authored hand"),Snapshot.CastHandAlignmentWeight(),0.f);
    T.Phase(EMCNutBossAttack::Fireball,EMCNutBossState::Recovery,0,.9);
    TestEqual(TEXT("Changing server phase does not restart the cast transition blend"),T.Snapshot().VisualKey,CastKey);
    int32 ChannelKey=0;
    for(const double Age:{0.,.275,.825,1.1,2.475,3.9}) {
        T.Phase(EMCNutBossAttack::NutRain,EMCNutBossState::Executing,Age,4.);
        Snapshot=T.Snapshot();
        auto* Sample=Find(Snapshot,Rain);
        if(!TestNotNull(TEXT("Rain uses the marked authored clip"),Sample)) return false;
        const float Expected=.55f+.30f*float(FMath::Fmod(Age/1.1,1.));
        TestEqual(TEXT("The complete channel advances forward through its closed gesture loop"),Sample->Seconds/Rain->GetPlayLength(),Expected,.002f);
        TestEqual(TEXT("Both rain palms remain free for continuous conducting"),Snapshot.CastHandAlignmentWeight(),0.f);
        ChannelKey=Snapshot.VisualKey;
    }
    T.Phase(EMCNutBossAttack::NutRain,EMCNutBossState::Recovery,0,1.4);
    Snapshot=T.Snapshot();
    TestEqual(TEXT("Channel recovery starts after the authored loop segment"),Find(Snapshot,Rain)->Seconds/Rain->GetPlayLength(),.85f,.001f);
    TestTrue(TEXT("Rain blends from an interrupted channel pose into its exit"),Snapshot.VisualKey!=ChannelKey);
    T.Phase(EMCNutBossAttack::Melee,EMCNutBossState::Recovery,0,.9);
    TestFalse(TEXT("The independently authored shove does not use spell metadata"),T.Snapshot().bAuthoredSpellGestures);
    return true;
}
#endif
