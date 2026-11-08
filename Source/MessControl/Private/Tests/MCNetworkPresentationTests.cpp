#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCNetworkPresentation.h"
#include "MCMotionAuthorityState.h"
#include "MCToothMeshComponent.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace MCNetworkPresentationTests
{
struct FVisualFixture
{
    UWorld* World;
    AActor* Actor;
    USceneComponent* Root;
    UMCToothMeshComponent* Mesh;
    USceneComponent* Tool;
    FMCOwnerReconciliationPresentation Lease;
    FMCNetworkCorrectionContext Context;
    FVisualFixture()
    {
        Context.LoadEpoch=FGuid::NewGuid();Context.OwnerRevision=Context.ResetRevision=1;
        World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Actor=World->SpawnActor<AActor>();Root=NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(Root);Root->RegisterComponent();
        Mesh=NewObject<UMCToothMeshComponent>(Actor);Mesh->SetupAttachment(Root);Mesh->RegisterComponent();
        Mesh->SetRelativeTransform(FTransform(FRotator(0,-90,0),FVector(0,0,-87),FVector(.9,1.1,1)));
        Tool=NewObject<USceneComponent>(Actor);Tool->SetupAttachment(Mesh);Tool->RegisterComponent();
        Tool->SetRelativeLocation(FVector(25,12,7));
        Actor->SetActorTransform(FTransform(FRotator(0,37,0),FVector(100,-80,30),FVector(1.2,.8,1.1)));
    }
    ~FVisualFixture()
    {
        Lease.Release();World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    void Correction(FVector Delta,bool Awaiting,double Time)
    {
        auto* Held=Lease.BeginScope();
        {FScopedPreventAttachedComponentMove Freeze(Held);Actor->SetActorLocation(Actor->GetActorLocation()+Delta,false,nullptr,ETeleportType::TeleportPhysics);}
        Lease.EndScope(Awaiting,Time);
    }
    void RotatingCorrection(FVector Delta,float Yaw,bool Awaiting,double Time)
    {
        auto* Held=Lease.BeginScope();
        {FScopedPreventAttachedComponentMove Freeze(Held);
            const FQuat Rotation=(FQuat(FVector::UpVector,FMath::DegreesToRadians(Yaw))*Actor->GetActorQuat()).GetNormalized();
            Actor->SetActorLocationAndRotation(Actor->GetActorLocation()+Delta,Rotation,false,nullptr,ETeleportType::TeleportPhysics);}
        Lease.EndScope(Awaiting,Time);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNetworkOwnerContinuity,"MessControl.Networking.SharedPresentation.NativeResponseReplayContinuity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNetworkOwnerContinuity::RunTest(const FString&)
{
    using namespace MCNetworkPresentationTests;
    for(int32 FPS:{30,60,120}) {
        FVisualFixture F;const auto Sim=F.Actor->GetActorLocation(),Visible=F.Mesh->GetComponentLocation(),Tool=F.Tool->GetComponentLocation();
        const auto Relative=F.Mesh->GetRelativeTransform();
        TestTrue(TEXT("Generic CMC domain acquires without any active skill"),F.Lease.Begin(F.Mesh,F.Context));
        F.Correction(FVector(15.512,0,0),true,0);F.Correction(FVector(236.222,0,0),false,0);
        TestTrue(TEXT("Canonical native response and full replay remain immediate"),F.Actor->GetActorLocation().Equals(Sim+FVector(251.734,0,0),.001));
        TestTrue(TEXT("Mesh and its attached ordinary tool remain continuous across both scopes"),F.Mesh->GetComponentLocation().Equals(Visible,.001) && F.Tool->GetComponentLocation().Equals(Tool,.001));
        TestTrue(TEXT("Authored scale and rotation survive correction"),F.Mesh->GetRelativeTransform().GetScale3D().Equals(Relative.GetScale3D(),.00001) && F.Mesh->GetRelativeTransform().GetRotation().Equals(Relative.GetRotation(),.00001));
        float Previous=F.Lease.Offset().Size();bool Monotonic=true,Once=true;
        for(int32 I=1;I<=FPS;++I) {
            F.Actor->SetActorLocation(F.Actor->GetActorLocation()+FVector(850./FPS,0,0));
            F.Lease.Tick(double(I)/FPS);const auto Shown=F.Mesh->GetComponentLocation();
            Monotonic&=F.Lease.Offset().Size()<=Previous+.001f;Previous=F.Lease.Offset().Size();
            F.Lease.Tick(double(I)/FPS);Once&=F.Mesh->GetComponentLocation().Equals(Shown,.00001);
        }
        TestTrue(TEXT("Decay is monotonic and advances once per world time at all sample rates"),Monotonic && Once);
        TestTrue(TEXT("Decay restores exact owned baseline"),!F.Lease.IsOwned() && F.Mesh->GetRelativeTransform().Equals(Relative,.001));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNetworkPresentationLifecycle,"MessControl.Networking.SharedPresentation.OwnershipAndZeroNetDeadline",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNetworkPresentationLifecycle::RunTest(const FString&)
{
    using namespace MCNetworkPresentationTests;FVisualFixture F;
    const auto Base=F.Mesh->GetRelativeTransform();
    F.Lease.Begin(F.Mesh,F.Context);F.Correction(FVector(250,0,0),false,0);F.Lease.Tick(.06);
    const auto Offset=F.Lease.Offset();
    F.Correction(FVector(-30,0,0),true,.07);F.Correction(FVector(30,0,0),false,.07);
    TestTrue(TEXT("Zero-net rewind/replay retains its prior offset"),F.Lease.Offset().Equals(Offset,.001));
    F.Lease.Tick(.251);TestTrue(TEXT("Zero-net positional ACK does not restart the original deadline"),!F.Lease.IsOwned() && F.Mesh->GetRelativeTransform().Equals(Base,.001));
    F.Lease.Begin(F.Mesh,F.Context);F.Correction(FVector(250,0,0),false,1);
    const auto Foreign=F.Mesh->GetRelativeLocation()+FVector(7,13,19);F.Mesh->SetRelativeLocation(Foreign);F.Lease.Release();
    TestTrue(TEXT("Foreign animation/attachment writes survive release"),F.Mesh->GetRelativeLocation().Equals(Foreign,.00001));
    F.Lease.Begin(F.Mesh,F.Context);F.Correction(FVector(100,0,0),false,2);
    F.Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);const auto Detached=F.Mesh->GetComponentTransform();F.Lease.Release();
    TestTrue(TEXT("Alternate body owner is never reattached or overwritten"),!F.Mesh->GetAttachParent() && F.Mesh->GetComponentTransform().Equals(Detached,.00001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNetworkFrameContract,"MessControl.Networking.SharedPresentation.FramesAndAuthorityOrdering",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNetworkFrameContract::RunTest(const FString&)
{
    FMCActorPresentationFrame Frame;
    Frame.GameplayActor=FTransform(FRotator(0,41,0),FVector(100,-20,60),FVector(1.2,.8,1.1));
    Frame.PresentedActor=Frame.GameplayActor;Frame.PresentedActor.AddToTranslation(FVector(-250,35,12));
    const FTransform Local(FRotator(3,12,8),FVector(25,12,7),FVector(.7));
    const auto Canonical=Local*Frame.GameplayActor,Shown=Frame.FromActorLocal(Local);
    TestTrue(TEXT("Canonical prop and local mount share one presentation frame"),Frame.FromCanonicalWorld(Canonical).Equals(Shown,.001));
    TestTrue(TEXT("Already evaluated socket is converted to gameplay only by the inverse mapping"),Frame.ToCanonicalWorld(Shown).Equals(Canonical,.001));
    TestTrue(TEXT("Canonical point roundtrip preserves nonuniform scale"),Frame.ToCanonicalWorld(Frame.FromCanonicalWorld(FVector(13,47,62))).Equals(FVector(13,47,62),.001));
    FMCNetworkCorrectionContext A;A.LoadEpoch=FGuid::NewGuid();A.OwnerRevision=4;A.ResetRevision=2;
    auto Body=A;Body.OwnerRevision++;Body.Owner=EMCPresentationOwner::BodyFrame;
    TestTrue(TEXT("A new explicit owner advances the common high water mark"),Body.CanAdvanceFrom(A,A.LoadEpoch));
    TestFalse(TEXT("Old CMC packet cannot revive movement after body handoff"),A.CanAdvanceFrom(Body,A.LoadEpoch));
    auto Reset=Body;Reset.ResetRevision++;
    TestFalse(TEXT("Older reset cannot overwrite an applied intentional teleport"),Body.CanAdvanceFrom(Reset,A.LoadEpoch));
    auto WrongOwner=A;WrongOwner.Owner=EMCPresentationOwner::Transit;
    TestFalse(TEXT("Owner cannot change without its server revision"),WrongOwner.CanAdvanceFrom(A,A.LoadEpoch));
    auto NewEpoch=A;NewEpoch.LoadEpoch=FGuid::NewGuid();NewEpoch.OwnerRevision=NewEpoch.ResetRevision=1;
    TestFalse(TEXT("Unordered foreign load epoch cannot replace a current owner"),NewEpoch.CanAdvanceFrom(A,FGuid()));
    TestTrue(TEXT("Only the shared current load epoch replaces an established epoch"),NewEpoch.CanAdvanceFrom(A,NewEpoch.LoadEpoch));
    TArray<uint8> Wire;FMemoryWriter Writer(Wire);TestTrue(TEXT("Native ACK context serializes"),Reset.Serialize(Writer));
    FMCNetworkCorrectionContext Read;FMemoryReader Reader(Wire);TestTrue(TEXT("Native ACK context roundtrips exactly"),Read.Serialize(Reader) && Read.Equals(Reset));
    using namespace MCNetworkPresentationTests;FVisualFixture F;auto* Hero=F.World->SpawnActor<AMCToothCharacter>();
    Hero->GetCharacterMovement()->Velocity=FVector(123,-17,8);
    const auto CanonicalBefore=Hero->GetSimulationFrame();
    if(auto* Visual=Cast<UMCToothMeshComponent>(Hero->GetMesh())) {
        Visual->BeginNetworkPresentationCompensation();
        Visual->SetRelativeLocation(Visual->GetRelativeLocation()+FVector(-250,35,12));
        Visual->EndNetworkPresentationCompensation();
    }
    const auto CanonicalAfter=Hero->GetSimulationFrame();
    TestTrue(TEXT("A displayed root offset cannot feed canonical center or velocity"),
        CanonicalAfter.GameplayActor.Equals(CanonicalBefore.GameplayActor,.001)
        && CanonicalAfter.PhysicalCenter.Equals(CanonicalBefore.PhysicalCenter,.001)
        && CanonicalAfter.CanonicalVelocity.Equals(FVector(123,-17,8),.001)
        && CanonicalAfter.bHasCanonicalPhysicsSample && CanonicalAfter.bHasSourceTime);
    const auto Before=Hero->CaptureNetworkCorrectionContext();Hero->SetActorLocation(FVector(600,0,900));Hero->NotifyNetworkPositionReset();
    const auto After=Hero->CaptureNetworkCorrectionContext();
    TestTrue(TEXT("Intentional position reset advances reset generation without inventing a new movement owner"),After.OwnerRevision==Before.OwnerRevision && After.ResetRevision!=Before.ResetRevision);
    TestNull(TEXT("Authority never enters autonomous correction presentation"),Hero->BeginNetworkPresentationCorrection());
    bool MoveFirst=false,CameraLast=false;
    for(const auto& P:Hero->PrimaryActorTick.GetPrerequisites()) MoveFirst|=P.Get()==&Hero->GetCharacterMovement()->PrimaryComponentTick;
    for(const auto& P:Hero->CameraBoom->PrimaryComponentTick.GetPrerequisites()) CameraLast|=P.Get()==&Hero->PrimaryActorTick;
    TestTrue(TEXT("Native CMC/replay precedes character camera, which precedes spring arm"),MoveFirst && CameraLast);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNetworkBodyFrameHandoff,"MessControl.Networking.SharedPresentation.BodyHandoffResetAndCanonicalSample",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNetworkBodyFrameHandoff::RunTest(const FString&)
{
    using namespace MCNetworkPresentationTests;FVisualFixture F;
    auto* Hero=F.World->SpawnActor<AMCToothCharacter>(FVector(100,0,900),FRotator::ZeroRotator);
    auto* PC=F.World->SpawnActor<APlayerController>();PC->SetAsLocalPlayerController();PC->Possess(Hero);
    auto* Physics=Hero->ToothPhysics.Get();const auto Initial=Hero->CaptureNetworkCorrectionContext();
    Physics->Frame.SetMotionContext(Initial);Physics->AcceptedFrame=Physics->Frame;
    Hero->SetRole(ROLE_AutonomousProxy);Hero->ObservedMotionContext=Hero->LastAppliedNativeContext=Initial;
    TestTrue(TEXT("Actual non-host owner can enter a generic correction transaction"),
        Hero->AdoptNetworkCorrectionContext(Initial)==EMCNetworkCorrectionDecision::Reconcile);
    {
        const FScopedPreventAttachedComponentMove Hold(Hero->BeginNetworkPresentationCorrection());
        Hero->SetActorLocation(Hero->GetActorLocation()+FVector(250,0,0));
    }
    Hero->EndNetworkPresentationCorrection(false,true);
    TestTrue(TEXT("The handoff starts with an outstanding real CMC display offset"),Hero->NetworkPresentation.Offset().Size()>200);
    const FTransform Shown=Hero->GetMesh()->GetComponentTransform();
    auto Body=Initial;Body.OwnerRevision++;Body.Owner=EMCPresentationOwner::BodyFrame;
    Physics->Frame.State=EMCBodyState::Ragdoll;Physics->Frame.SetMotionContext(Body);
    Physics->Frame.CapsuleLocation=Hero->GetActorLocation();
    Physics->Frame.MeshLocation=Shown.GetLocation()+FVector(250,0,0);Physics->Frame.MeshRotation=Shown.Rotator();
    Physics->Frame.PhysicalCenter=FVector(350,0,900);Physics->Frame.CanonicalVelocity=FVector(321,12,-40);Physics->Frame.SampledAt=77.f;
    Physics->OnRep_Frame();
    TestTrue(TEXT("Ordinary CMC to ragdoll handoff keeps the shown mesh when it detaches"),
        !Hero->GetMesh()->GetAttachParent() && Hero->GetMesh()->GetComponentTransform().Equals(Shown,.01));
    const auto Sample=Hero->GetSimulationFrame();
    TestTrue(TEXT("A remote non-simulating ragdoll exposes accepted canonical physics, never displayed bones or local Chaos velocity"),
        !Hero->GetMesh()->IsSimulatingPhysics() && Sample.bHasCanonicalPhysicsSample && Sample.bHasSourceTime
        && Sample.CanonicalVelocity.Equals(FVector(321,12,-40),.001) && Sample.PhysicalCenter.Equals(FVector(350,0,900),.001) && Sample.SourceTime==77.);
    Physics->TickComponent(.016f,LEVELTICK_All,nullptr);
    const FVector Eased=Hero->GetMesh()->GetComponentLocation();
    TestTrue(TEXT("Selected body adapter converges after the handoff"),Eased.X>Shown.GetLocation().X && Eased.X<FVector(Physics->Frame.MeshLocation).X);
    Body.ResetRevision++;Physics->Frame.SetMotionContext(Body);
    Physics->Frame.CapsuleLocation=FVector(900,20,1000);Physics->Frame.MeshLocation=FVector(900,20,942);
    Physics->OnRep_Frame();
    TestTrue(TEXT("Explicit reset snaps body capsule and shown mesh exactly once"),
        Hero->GetActorLocation().Equals(FVector(900,20,1000),.001) && Hero->GetMesh()->GetComponentLocation().Equals(FVector(900,20,942),.001));
    const auto ResetPose=Hero->GetMesh()->GetComponentTransform();
    Physics->Frame.MeshLocation=FVector(1000,20,942);Physics->OnRep_Frame();
    TestTrue(TEXT("A following same-context packet resumes normal body interpolation"),Hero->GetMesh()->GetComponentTransform().Equals(ResetPose,.001));
    auto Old=Body;Old.ResetRevision--;Physics->Frame.SetMotionContext(Old);Physics->Frame.MeshLocation=FVector(-1000);
    Physics->OnRep_Frame();
    TestTrue(TEXT("Late pre-reset body packet cannot restore old state or pose"),Hero->GetMesh()->GetComponentTransform().Equals(ResetPose,.001) && Physics->Frame.MotionContext().Equals(Body));
    auto* State=F.World->SpawnActor<AMCGameState>();F.World->SetGameState(State);State->GameplayLoadId=FGuid::NewGuid();
    auto NextEpoch=Body;NextEpoch.LoadEpoch=State->GameplayLoadId;NextEpoch.OwnerRevision=NextEpoch.ResetRevision=1;
    Physics->Frame.SetMotionContext(NextEpoch);Physics->Frame.CapsuleLocation=FVector(1500,20,1000);Physics->Frame.MeshLocation=FVector(1500,20,942);
    Physics->OnRep_Frame();
    TestTrue(TEXT("A published new load epoch also snaps the accepted body frame"),Hero->GetMesh()->GetComponentLocation().Equals(FVector(1500,20,942),.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNetworkOwnerRotationContinuity,
    "MessControl.Networking.SharedPresentation.NativeRotationResponseReplayContinuity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNetworkOwnerRotationContinuity::RunTest(const FString&)
{
    using namespace MCNetworkPresentationTests;
    for(int32 FPS:{30,60,120}) for(bool AbsoluteRotation:{false,true}) {
        FVisualFixture F;F.Mesh->SetAbsolute(false,AbsoluteRotation,false);
        const FTransform Relative=F.Mesh->GetRelativeTransform(),Shown=F.Mesh->GetComponentTransform(),ToolShown=F.Tool->GetComponentTransform();
        const FQuat CanonicalStart=F.Actor->GetActorQuat();
        TestTrue(TEXT("The generic mesh acquires its authored baseline under a nonuniform parent"),F.Lease.Begin(F.Mesh,F.Context));
        F.RotatingCorrection(FVector(15,0,0),90,true,0);
        TestTrue(TEXT("Native positional response rotates canonical movement immediately by 90 degrees"),
            FMath::IsNearlyEqual(CanonicalStart.AngularDistance(F.Actor->GetActorQuat()),double(HALF_PI),.00001));
        TestTrue(TEXT("The actual body and attached tool keep world position AND orientation across the native response"),
            F.Mesh->GetComponentTransform().Equals(Shown,.001) && F.Tool->GetComponentTransform().Equals(ToolShown,.001));
        F.RotatingCorrection(FVector(25,14,0),90,false,0);
        TestTrue(TEXT("Full replay rotates canonical movement by another 90 degrees without delaying simulation"),
            FMath::IsNearlyEqual(CanonicalStart.AngularDistance(F.Actor->GetActorQuat()),double(PI),.00001));
        TestTrue(TEXT("The actual body and attached tool remain continuous through both native scopes"),
            F.Mesh->GetComponentTransform().Equals(Shown,.001) && F.Tool->GetComponentTransform().Equals(ToolShown,.001));
        TestTrue(TEXT("Authored relative scale and absolute-rotation policy survive compensation"),
            F.Mesh->GetRelativeScale3D().Equals(Relative.GetScale3D(),.00001) && F.Mesh->IsUsingAbsoluteRotation()==AbsoluteRotation);
        double PreviousAngle=PI;bool Monotonic=true,Once=true,ToolAttached=true;
        for(int32 I=1;I<=FPS;++I) {
            F.Actor->SetActorLocation(F.Actor->GetActorLocation()+FVector(120./FPS,20./FPS,0));
            F.Actor->SetActorRotation((FQuat(FVector::UpVector,FMath::DegreesToRadians(30./FPS))*F.Actor->GetActorQuat()).GetNormalized());
            F.Lease.Tick(double(I)/FPS);
            const FTransform Current=F.Mesh->GetComponentTransform();
            const FQuat AuthoredWorld=AbsoluteRotation?Relative.GetRotation():(F.Actor->GetActorQuat()*Relative.GetRotation()).GetNormalized();
            const double Angle=Current.GetRotation().AngularDistance(AuthoredWorld);
            Monotonic&=Angle<=PreviousAngle+.00001;PreviousAngle=Angle;
            ToolAttached&=F.Tool->GetComponentTransform().Equals(F.Tool->GetRelativeTransform()*Current,.001);
            F.Lease.Tick(double(I)/FPS);Once&=F.Mesh->GetComponentTransform().Equals(Current,.00001);
        }
        TestTrue(TEXT("Rotation converges monotonically once per time while ordinary canonical motion continues at 30/60/120Hz"),Monotonic && Once && ToolAttached);
        TestTrue(TEXT("Completion restores the exact authored relative baseline and flags"),
            !F.Lease.IsOwned() && F.Mesh->GetRelativeTransform().Equals(Relative,.001) && F.Mesh->IsUsingAbsoluteRotation()==AbsoluteRotation);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCNetworkOwnerRotationDeadline,
    "MessControl.Networking.SharedPresentation.RotationZeroNetDeadlineAndForeignOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCNetworkOwnerRotationDeadline::RunTest(const FString&)
{
    using namespace MCNetworkPresentationTests;FVisualFixture F;
    const FTransform Base=F.Mesh->GetRelativeTransform();
    F.Lease.Begin(F.Mesh,F.Context);F.RotatingCorrection(FVector::ZeroVector,180,false,0);
    TestTrue(TEXT("A pure rotation error retains presentation ownership even with zero translation"),F.Lease.IsOwned());
    F.Lease.Tick(.03);const FTransform Shown=F.Mesh->GetComponentTransform(),ToolShown=F.Tool->GetComponentTransform();
    F.RotatingCorrection(FVector::ZeroVector,-25,true,.07);F.RotatingCorrection(FVector::ZeroVector,25,false,.07);
    TestTrue(TEXT("A rotational rewind plus equal replay preserves the actual shown body and tool"),
        F.Mesh->GetComponentTransform().Equals(Shown,.001) && F.Tool->GetComponentTransform().Equals(ToolShown,.001));
    // The unchanged distance-only duration is .12s here. A restarted deadline
    // would be .19s; .121 proves that zero-net rotation did not extend it.
    F.Lease.Tick(.121);
    TestTrue(TEXT("Zero-net rotational responses cannot restart the original decay deadline"),
        !F.Lease.IsOwned() && F.Mesh->GetRelativeTransform().Equals(Base,.001));
    F.Lease.Begin(F.Mesh,F.Context);F.RotatingCorrection(FVector::ZeroVector,70,false,1);
    const FQuat Foreign=(FQuat(FVector::UpVector,FMath::DegreesToRadians(15.))*F.Mesh->GetRelativeRotation().Quaternion()).GetNormalized();
    F.Mesh->SetRelativeRotation(Foreign);F.Lease.Release();
    TestTrue(TEXT("A foreign relative rotation is preserved during cleanup"),F.Mesh->GetRelativeRotation().Quaternion().Equals(Foreign,.00001));
    return true;
}
#endif
