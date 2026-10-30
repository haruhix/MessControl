#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCActiveSkillMotion.h"
#include "MCSpongeSkillAction.h"
#include "MCDriverSkillAction.h"
#include "MCSuperCarVisualComponent.h"
#include "MCHookSkillAction.h"
#include "MCBossCharacter.h"
#include "MCNutBoss.h"
#include "MCPerkComponent.h"
#include "MCPlayerController.h"
#include "MCPlayerState.h"
#include "MCGameState.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothStatusComponent.h"
#include "MCMouthSurface.h"
#include "MCCoffeeWipe.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/Package.h"

namespace MCActiveSkillMotionTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    UPackage* OwnedWorldPackage=World->GetOutermost();
    const FName OwnedWorldPackageName=OwnedWorldPackage->GetFName();
    AMCToothCharacter* Hero=nullptr;
    AMCPlayerState* Player=nullptr;
    AMCPlayerController* Controller=nullptr;
    UMCToothMovementComponent* Move=nullptr;
    AActor* FloorActor=nullptr;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);
        World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* Game=World->SpawnActor<AMCGameState>();World->SetGameState(Game);
        Game->Phase=EMCShiftPhase::Working;Game->bLobbyWaiting=false;
        FloorActor=Box(FVector(0,0,9980),FVector(5000,5000,20));
        Controller=World->SpawnActor<AMCPlayerController>();Controller->SetAsLocalPlayerController();
        Player=World->SpawnActorDeferred<AMCPlayerState>(AMCPlayerState::StaticClass(),FTransform::Identity);
        auto* Table=NewObject<UDataTable>(World);Table->RowStruct=FMCPerkDefinition::StaticStruct();
        Player->Perks->PerkTable=Table;Player->FinishSpawning(FTransform::Identity);
        Controller->SetPlayerState(Player);Player->SetOwner(Controller);Game->AddPlayerState(Player);
        Hero=World->SpawnActor<AMCToothCharacter>(FVector(0,0,10085),FRotator::ZeroRotator);
        Controller->Possess(Hero);Controller->bShowMouseCursor=false;Hero->Status->Initialize(100);
        Move=CastChecked<UMCToothMovementComponent>(Hero->GetCharacterMovement());
        Step(.15f);
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
        // This exact newly-created fixture package has no live map after
        // teardown. Never clear dirty flags on any source/map/asset package.
        if(IsValid(OwnedWorldPackage) && OwnedWorldPackage->GetFName()==OwnedWorldPackageName
            && OwnedWorldPackage->GetName().StartsWith(TEXT("/Temp/")))
            OwnedWorldPackage->SetDirtyFlag(false);
    }
    AActor* Box(FVector At,FVector Extent)
    {
        auto* A=World->SpawnActor<AActor>();auto* C=NewObject<UBoxComponent>(A);A->SetRootComponent(C);
        C->SetBoxExtent(Extent);C->SetCollisionProfileName(TEXT("BlockAll"));C->RegisterComponent();A->SetActorLocation(At);return A;
    }
    void Step(float Seconds)
    {for(float At=0;At<Seconds-UE_SMALL_NUMBER;At+=1.f/60) {++GFrameCounter;World->Tick(LEVELTICK_All,FMath::Min(1.f/60,Seconds-At));}}
    template<class TEffect>
    AMCActiveSkillAction* Prepare(TMap<FName,float> Values={})
    {
        auto* Effect=NewObject<TEffect>(Player->Perks);Effect->OwnerComponent=Player->Perks;
        FMCPerkDefinition Row;Row.Parameters=MoveTemp(Values);
        FMCActiveSkillInputHandle Input;Input.SlotIndex=0;Input.SlotInstanceID=FGuid::NewGuid();Input.PressSequence=1;
        return Effect->PrepareAction(TEXT("MotionFixture"),Row,Input);
    }
    AMCToothCharacter* Ally(float X)
    {
        auto* H=World->SpawnActor<AMCToothCharacter>(FVector(X,0,10085),FRotator::ZeroRotator);
        auto* C=World->SpawnActor<APlayerController>();C->SetAsLocalPlayerController();C->Possess(H);H->Status->Initialize(100);
        return H;
    }
    void AimForward()
    {
        Hero->CameraOrbitHeight=30;Hero->SetCameraOrbitView(0,0,300);Controller->SetControlRotation(FRotator::ZeroRotator);
        Controller->SetViewTarget(Hero);Step(.1f);
    }
};

void BuildHookSupport(UProceduralMeshComponent* Surface,bool Wall,bool Ceiling)
{
    const float X[]={-300,40,100,500},Z[]={0,0,24,24};TArray<FVector> V,N;TArray<int32> Faces;
    for(int32 I=0;I<4;++I) {V.Add(FVector(X[I],-300,Z[I]));V.Add(FVector(X[I],300,Z[I]));}
    for(int32 I=0;I<3;++I) {const int32 J=I*2;Faces.Append({J,J+2,J+3,J,J+3,J+1});}
    if(Wall) {const int32 J=V.Num();V.Append({FVector(70,-150,0),FVector(70,150,0),FVector(70,150,180),FVector(70,-150,180)});Faces.Append({J,J+1,J+2,J,J+2,J+3});}
    if(Ceiling) {const int32 J=V.Num();V.Append({FVector(40,-150,110),FVector(40,150,110),FVector(120,150,110),FVector(120,-150,110)});Faces.Append({J,J+1,J+2,J,J+2,J+3});}
    // PMC GetPhysicsTriMeshData sets bFlipNormals=true; Chaos cooking swaps
    // v0/v1. Author reverse winding here so the COOKED floor faces up and roof
    // faces down. Render normals do not determine collision-triangle winding.
    for(int32 I=0;I<Faces.Num();I+=3) Swap(Faces[I],Faces[I+1]);
    N.Init(FVector::UpVector,V.Num());Surface->CreateMeshSection(0,V,Faces,N,TArray<FVector2D>(),TArray<FColor>(),TArray<FProcMeshTangent>(),true);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSuperCarIndependentPivots,"MessControl.ActiveSkills.Motion.SuperCarReusableSteeringAndIndependentBrushes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSuperCarIndependentPivots::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;
    auto* Owner=T.World->SpawnActor<AActor>();
    auto* Visual=NewObject<UMCSuperCarVisualComponent>(Owner);
    Owner->SetRootComponent(Visual);Owner->AddInstanceComponent(Visual);Visual->RegisterComponent();
    Visual->RebuildVisuals();
    if(!TestEqual(TEXT("A generic actor builds nine separate visible parts without an ability or pawn owner"),Visual->MeshParts.Num(),9)) return false;
    TMap<FName,FVector> Pivots;
    for(const auto& Pair:Visual->MeshParts) {
        if(!TestNotNull(Pair.Key.ToString()+TEXT(" resolves its actual saved mesh"),Pair.Value->GetStaticMesh().Get())) return false;
        Pivots.Add(Pair.Key,Pair.Value->GetRelativeLocation());
    }
    auto* Front=Visual->GetPart(TEXT("WheelFrontLeft"));
    auto* Rear=Visual->GetPart(TEXT("WheelRearLeft"));
    auto* Brush=Visual->GetPart(TEXT("BrushFrontLeft"));
    if(!TestTrue(TEXT("Front/rear wheels and brushes are independent components"),Front && Rear && Brush && Front!=Rear && Rear!=Brush)) return false;
    Visual->SetSteeringAngle(20);Visual->SetTravelDistance(100);
    const FQuat FrontBefore=Front->GetRelativeRotation().Quaternion(),RearBefore=Rear->GetRelativeRotation().Quaternion();
    const FQuat BrushBefore=Brush->GetRelativeRotation().Quaternion();
    Visual->SetBrushSpeed(180);Visual->UpdateTravel(0,20,.1f);
    TestTrue(TEXT("Stopped wheels keep their roll while powered brushes continue"),FrontBefore.Equals(Front->GetRelativeRotation().Quaternion(),.0001f) && RearBefore.Equals(Rear->GetRelativeRotation().Quaternion(),.0001f)
        && !BrushBefore.Equals(Brush->GetRelativeRotation().Quaternion(),.001f));
    const FVector SteeredAxle=FRotator(0,20,0).RotateVector(FVector::RightVector);
    TestTrue(TEXT("A rolling front wheel preserves its own steered axle"),FVector::DotProduct(Front->GetRelativeRotation().RotateVector(FVector::RightVector),SteeredAxle)>.999);
    TestTrue(TEXT("Rear wheels roll without copying front steering"),FVector::DotProduct(Rear->GetRelativeRotation().RotateVector(FVector::RightVector),FVector::RightVector)>.999);
    Visual->SetSteeringAngle(-20);Visual->SetTravelDistance(-100);
    for(const auto& Pair:Visual->MeshParts) TestTrue(Pair.Key.ToString()+TEXT(" turns in place around its mechanical pivot"),Pair.Value->GetRelativeLocation().Equals(Pivots.FindChecked(Pair.Key),.0001));
    Visual->SetSteeringAngle(100);
    TestTrue(TEXT("Authored steering has a bounded angle"),Visual->CurrentSteeringAngle<=25.f);
    Owner->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveMotionRaisedSupport,"MessControl.ActiveSkills.Motion.WalkableSupportLiftPreservesWalls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveMotionRaisedSupport::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;
    T.FloorActor->Destroy();
    auto* SupportActor=T.World->SpawnActor<AActor>();
    auto* Support=NewObject<UProceduralMeshComponent>(SupportActor);SupportActor->SetRootComponent(Support);
    Support->bUseComplexAsSimpleCollision=true;Support->bUseAsyncCooking=false;
    Support->SetCollisionProfileName(TEXT("BlockAll"));Support->SetCollisionObjectType(ECC_WorldStatic);
    Support->CanCharacterStepUpOn=ECB_Yes;Support->RegisterComponent();SupportActor->SetActorLocation(FVector(0,0,10000));
    auto BuildSupport=[&](bool WithWall) {
        // Flat under the 34cm capsule, then a shallow walkable rise under the
        // wider 80cm ride box. All triangles belong to the SAME support body.
        const float X[]={-300,40,100,300},Z[]={0,0,24,24};
        TArray<FVector> Vertices,Normals;TArray<int32> Triangles;
        for(int32 I=0;I<4;++I) {Vertices.Add(FVector(X[I],-300,Z[I]));Vertices.Add(FVector(X[I],300,Z[I]));}
        for(int32 I=0;I<3;++I) {const int32 A=I*2;Triangles.Append({A,A+2,A+3,A,A+3,A+1});}
        if(WithWall) {
            const int32 A=Vertices.Num();Vertices.Append({FVector(70,-150,0),FVector(70,150,0),FVector(70,150,180),FVector(70,-150,180)});
            Triangles.Append({A,A+1,A+2,A,A+2,A+3});
        }
        Normals.Init(FVector::UpVector,Vertices.Num());
        Support->CreateMeshSection(0,Vertices,Triangles,Normals,TArray<FVector2D>(),TArray<FColor>(),TArray<FProcMeshTangent>(),true);
        T.Move->bForceNextFloorCheck=true;T.Step(.1f);
    };
    BuildSupport(false);
    FVector Point,Normal;
    if(!TestTrue(TEXT("Native CMC finds the new walkable support"),MCActiveSkillMotion::HasGround(T.Hero,Point,Normal)
        && T.Move->CurrentFloor.HitResult.GetComponent()==Support)) return false;
    const FVector Half(80,42,12),Center=Point+Normal*18;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCActiveRideRaisedSupportFixture),false,T.Hero);FHitResult Contact;
    TestTrue(TEXT("The uncorrected tangent box really starts penetrating this raised support"),
        T.World->SweepSingleByChannel(Contact,Center,Center,FRotationMatrix::MakeFromZX(Normal,FVector::ForwardVector).ToQuat(),
            ECC_Pawn,FCollisionShape::MakeBox(Half),Query) && Contact.bStartPenetrating && Contact.GetComponent()==Support && Contact.PenetrationDepth>2.f);
    TestTrue(TEXT("A bounded lift clears the native uneven support without ignoring it"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,0,0));
    TestTrue(TEXT("The whole forward sweep also clears its walkable rising contact"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,.05f,200.f));
    const float StepHeight=T.Move->MaxStepHeight;T.Move->MaxStepHeight=2;
    TestFalse(TEXT("Support deeper than the configured native step allowance remains blocked"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,0,0));
    T.Move->MaxStepHeight=StepHeight;
    auto* Wall=T.Box(FVector(70,0,10080),FVector(3,150,80));
    TestFalse(TEXT("A separate vertical wall still blocks after the support correction"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,0,0));
    Wall->Destroy();BuildSupport(true);
    TestFalse(TEXT("A vertical wall on the SAME support component is never ignored"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,0,0));
    TestTrue(TEXT("Only queries were lifted; the hero retains native ground movement"),T.Move->IsMovingOnGround());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveMotionLateralSupport,"MessControl.ActiveSkills.Motion.LateralSupportMTDUsesNearGeometricPlane",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveMotionLateralSupport::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;
    T.FloorActor->Destroy();
    auto* SupportActor=T.World->SpawnActor<AActor>();
    auto* Support=NewObject<UProceduralMeshComponent>(SupportActor);SupportActor->SetRootComponent(Support);
    Support->bUseComplexAsSimpleCollision=true;Support->bUseAsyncCooking=false;
    Support->SetCollisionProfileName(TEXT("BlockAll"));Support->SetCollisionObjectType(ECC_WorldStatic);
    Support->CanCharacterStepUpOn=ECB_Yes;Support->RegisterComponent();SupportActor->SetActorLocation(FVector(0,0,10000));
    float RaisedZ=35,CornerX=120,CornerY=100;
    auto BuildSupport=[&](bool WithWall,bool WithCeiling) {
        // The capsule has a flat floor. A walkable raised triangle near the
        // wide vehicle's corner can have a shorter SIDEWAYS escape than its
        // vertical depth. All these faces belong to one real Chaos body.
        TArray<FVector> Vertices={FVector(-400,-400,0),FVector(400,-400,0),FVector(400,400,0),FVector(-400,400,0),
            FVector(CornerX,CornerY,RaisedZ),FVector(CornerX+60,CornerY,RaisedZ),FVector(CornerX,CornerY+80,RaisedZ)};
        TArray<int32> Triangles={0,1,2,0,2,3,4,5,6};
        if(WithWall) {
            const int32 A=Vertices.Num();Vertices.Append({FVector(130,80,0),FVector(130,170,0),FVector(130,170,180),FVector(130,80,180)});
            Triangles.Append({A,A+1,A+2,A,A+2,A+3});
        }
        if(WithCeiling) {
            // Outside the narrow player capsule, just above the unlifted box.
            // A query lifted away from the floor must still see this thin roof.
            const int32 A=Vertices.Num();Vertices.Append({FVector(100,80,145),FVector(100,170,145),FVector(180,170,145),FVector(180,80,145)});
            Triangles.Append({A,A+1,A+2,A,A+2,A+3});
        }
        TArray<FVector> Normals;Normals.Init(FVector::UpVector,Vertices.Num());
        Support->CreateMeshSection(0,Vertices,Triangles,Normals,TArray<FVector2D>(),TArray<FColor>(),TArray<FProcMeshTangent>(),true);
        T.Move->bForceNextFloorCheck=true;T.Step(.025f);
    };
    const FVector Half(145,125,65);
    FVector Point,Normal;FHitResult Raw,Geometric;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCActiveRideLateralSupportFixture),false,T.Hero);
    FCollisionQueryParams FaceQuery(SCENE_QUERY_STAT(MCActiveRideLateralFaceFixture),true);FaceQuery.bReturnFaceIndex=true;
    bool Reproduced=false;
    // A bounded geometry search tolerates Chaos choosing a different edge at
    // exact ties. It must find a REAL lateral MTD and nearby walkable face;
    // no normal is fabricated and production is not called until this holds.
    for(float Height:{35.f,40.f}) {
        for(float X:{120.f,125.f,130.f}) {
            for(float Y:{100.f,105.f,110.f}) {
                RaisedZ=Height;CornerX=X;CornerY=Y;BuildSupport(false,false);
                if(!MCActiveSkillMotion::HasGround(T.Hero,Point,Normal) || T.Move->CurrentFloor.HitResult.GetComponent()!=Support) continue;
                const FVector Center=Point+Normal*(Half.Z+6.f);
                const FQuat Heading=FRotationMatrix::MakeFromZX(Normal,FVector::ForwardVector).ToQuat();
                const bool Overlap=T.World->SweepSingleByChannel(Raw,Center,Center,Heading,ECC_Pawn,FCollisionShape::MakeBox(Half),Query);
                if(!Overlap || !Raw.bStartPenetrating || Raw.GetComponent()!=Support || Raw.PenetrationDepth<=2
                    || Raw.ImpactNormal.Z>=T.Move->GetWalkableFloorZ()) continue;
                if(Support->LineTraceComponent(Geometric,Raw.ImpactPoint+Normal*6.f,Raw.ImpactPoint-Normal*6.f,FaceQuery)
                    && Geometric.GetComponent()==Support && !Geometric.bStartPenetrating && Geometric.ImpactNormal.Z>.99f) {Reproduced=true;break;}
            }
            if(Reproduced) break;
        }
        if(Reproduced) break;
    }
    if(!TestTrue(TEXT("Real Chaos overlap has a lateral MTD beside a walkable geometric face"),Reproduced)) return false;
    AddInfo(FString::Printf(TEXT("Lateral fixture: raised=%.1f corner=(%.1f,%.1f) mtd=%s depth=%.5f actual_face=%s face_index=%d"),
        RaisedZ,CornerX,CornerY,*Raw.ImpactNormal.ToString(),Raw.PenetrationDepth,*Geometric.ImpactNormal.ToString(),Geometric.FaceIndex));
    const FVector HeroBefore=T.Hero->GetActorLocation();
    TestTrue(TEXT("Independent near-contact plane clears the same-component lateral overlap"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,0,0));
    TestTrue(TEXT("The complete short forward corridor also clears its support"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,.05f,200.f));
    TestTrue(TEXT("Query recovery never teleports the pawn"),T.Hero->GetActorLocation().Equals(HeroBefore,.001));
    const float StepHeight=T.Move->MaxStepHeight;T.Move->MaxStepHeight=2;
    TestFalse(TEXT("The actual plane cannot bypass the native step allowance"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,0,0));
    T.Move->MaxStepHeight=StepHeight;
    auto* ForeignWall=T.Box(FVector(130,120,10080),FVector(3,45,80));
    TestFalse(TEXT("A foreign obstacle beside the raised corner remains solid"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,0,0));
    ForeignWall->Destroy();BuildSupport(true,false);
    TestFalse(TEXT("A vertical face on the SAME support remains a wall"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,0,0));
    BuildSupport(false,true);
    TestFalse(TEXT("Successive lifted boxes cannot jump a thin same-component ceiling"),
        MCActiveSkillMotion::ClearRideFootprint(T.Hero,nullptr,FVector::ForwardVector,Half,0,0));
    TestTrue(TEXT("Native grounded player collision remains enabled"),T.Move->IsMovingOnGround());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveMotionLeaseIsolation,"MessControl.ActiveSkills.Motion.LeaseOwnsOnlyItsSourceAndMode",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveMotionLeaseIsolation::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;
    const float Walk=T.Move->WalkSpeed;
    auto Wind=MakeShared<FMCLocomotionRootMotionSource>();Wind->InstanceName=TEXT("MotionFixtureWind");Wind->Duration=-1;
    Wind->Priority=100;Wind->AccumulateMode=ERootMotionAccumulateMode::Additive;Wind->Force=FVector(0,25,0);
    const uint16 WindID=T.Move->ApplyRootMotionSource(Wind);
    FMCActiveSkillMotionLease First,Other;
    TestTrue(TEXT("A motion action can coexist with an unrelated additive wind"),First.Begin(T.Hero,FGuid::NewGuid(),FVector(250,0,0),true));
    TestFalse(TEXT("A second skill cannot replace the first skill's motion on the same pawn"),Other.Begin(T.Hero,FGuid::NewGuid(),FVector(-250,0,0),true));
    First.End();
    TestTrue(TEXT("Ending the action preserves the unrelated source"),T.Move->GetRootMotionSourceByID(WindID).IsValid());
    TestEqual(TEXT("No global walking-speed tuning changed"),T.Move->WalkSpeed,Walk);
    TestTrue(TEXT("Our temporary free-flight mode is restored"),T.Move->MovementMode!=MOVE_Flying);
    TestTrue(TEXT("The next lease has a fresh identity"),Other.Begin(T.Hero,FGuid::NewGuid(),FVector(250,0,0),true));
    T.Move->SetMovementMode(MOVE_None);Other.End();First.End();
    TestEqual(TEXT("Cleanup cannot revive a movement mode claimed by another system"),T.Move->MovementMode.GetValue(),MOVE_None);
    T.Move->SetMovementMode(MOVE_Walking);
    FMCActiveSkillMotionLease Expiring;
    TestTrue(TEXT("A bounded pull accepts its own source deadline"),Expiring.Begin(T.Hero,FGuid::NewGuid(),FVector(100,0,0),true,.05f));
    T.Step(.12f);Expiring.End();
    TestTrue(TEXT("An already-expired source still releases its temporary flight mode"),T.Move->MovementMode!=MOVE_Flying);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCActiveMotionCleaning,"MessControl.ActiveSkills.Motion.NativeMaskPathAndDiscontinuity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCActiveMotionCleaning::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;
    const FTransform Pose(FVector(0,0,10000));
    auto* Patch=T.World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
    Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=200;Patch->LiquidSeed=17;Patch->FinishSpawning(Pose);
    Patch->Status->ApplyCoffee(1);Patch->ResetLiquid();
    const FVector A(-70,0,10000),B(70,0,10000);
    const auto Before=Patch->WipeMask;
    TestTrue(TEXT("A supported local path changes the real native stain mask"),
        MCActiveSkillMotion::CleanSweep(T.Hero,nullptr,A,B,.2f,32.f,1.f,720.f));
    TestTrue(TEXT("Native wipe bytes changed without technical clear"),Patch->WipeMask!=Before);
    const int32 OffPath=(FMCCoffeeWipe::Size*3/4)*FMCCoffeeWipe::Size+FMCCoffeeWipe::Size/2;
    TestTrue(TEXT("The footprint does not clean the adjacent lane"),Patch->WipeMask.IsValidIndex(OffPath) && Patch->WipeMask[OffPath]==255);
    const auto Local=Patch->WipeMask;
    TestFalse(TEXT("A teleport never paints a cleaning line across the map"),
        MCActiveSkillMotion::CleanSweep(T.Hero,nullptr,A,B+FVector(3000,0,0),.016f,32.f,1.f,720.f));
    TestFalse(TEXT("A long missing sample never grants a clamped fake cleaning dose"),
        MCActiveSkillMotion::CleanSweep(T.Hero,nullptr,A,B,2.f,32.f,1.f,720.f));
    TestTrue(TEXT("Rejected paths preserve the exact mask"),Patch->WipeMask==Local);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSpongeRepeatAndDeadline,"MessControl.ActiveSkills.Motion.SpongeRepeatAndIndependentDeadline",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSpongeRepeatAndDeadline::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;
    auto* First=T.Prepare<UMCSpongeSkillEffect>({{TEXT("RideSeconds"),.6f},{TEXT("RideSpeed"),200.f}});
    if(!TestNotNull(TEXT("Grounded sponge can be prepared through the real factory"),First)) return false;
    First->StartCommitted();T.Step(.2f);
    TestTrue(TEXT("Repeating the accepted input stops the current ride"),First->RepeatInput());
    TestTrue(TEXT("Stopping does not need a new activation or cooldown check"),First->IsFinished());
    auto* Second=T.Prepare<UMCSpongeSkillEffect>({{TEXT("RideSeconds"),1.1f},{TEXT("RideSpeed"),200.f}});
    if(!TestNotNull(TEXT("A new ride gets an independent action and motion source"),Second)) return false;
    Second->StartCommitted();T.Step(.5f);
    TestFalse(TEXT("The first ride's old deadline cannot finish the second ride"),Second->IsFinished());
    T.Step(.7f);TestTrue(TEXT("The second ride ends at its own deadline"),Second->IsFinished());
    TestTrue(TEXT("Every owned motion source is released"),MCActiveSkillMotion::CanAcquire(T.Hero));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDriverFixedDuration,"MessControl.ActiveSkills.Motion.DriverThreeSecondsKeepsCollision",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDriverFixedDuration::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;
    auto* Driver=T.Prepare<UMCDriverSkillEffect>({{TEXT("RideSeconds"),99.f},{TEXT("RideSpeed"),200.f}});
    if(!TestNotNull(TEXT("Driver prepares on a clear supported footprint"),Driver)) return false;
    Driver->StartCommitted();T.Step(2.8f);
    TestFalse(TEXT("Driver is still active before the contracted three seconds"),Driver->IsFinished());
    T.Step(.3f);TestTrue(TEXT("Driver ends at three seconds despite a conflicting provisional data duration"),Driver->IsFinished());
    TestTrue(TEXT("The rider retained ordinary grounded capsule collision"),T.Move->IsMovingOnGround());
    TestTrue(TEXT("Driver's movement override is released"),MCActiveSkillMotion::CanAcquire(T.Hero));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDriverHandFrames,"MessControl.ActiveSkills.Motion.DriverHandFramesPreserveRotationAndPositionOverrides",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDriverHandFrames::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;
    auto* Driver=Cast<AMCDriverSkillAction>(T.Prepare<UMCDriverSkillEffect>({{TEXT("RightGripX"),23.f},{TEXT("LeftGripY"),-31.f}}));
    if(!TestNotNull(TEXT("The real Driver factory prepares authored hand frames"),Driver)) return false;
    auto* Visual=Driver->SuperCarVisual.Get();
    if(!TestNotNull(TEXT("Driver owns the reusable SuperCar presentation"),Visual)) return false;
    TestFalse(TEXT("Left default brace wrist has an authored nonidentity orientation"),Visual->LeftHandRotation.IsNearlyZero());
    TestFalse(TEXT("Right default brace wrist has an authored nonidentity orientation"),Visual->RightHandRotation.IsNearlyZero());
    TestTrue(TEXT("Both default wrists sit above the actual 122cm cockpit rim"),Visual->LeftHandLocal.Z>122 && Visual->RightHandLocal.Z>122);
    Visual->LeftHandLocal=FVector(-22,-27,138);Visual->RightHandLocal=FVector(-21,27,139);
    Visual->LeftHandRotation=FRotator(22,-31,15);Visual->RightHandRotation=FRotator(-12,90,-25);
    Visual->SetRelativeTransform(FTransform(FRotator(7,22,11),FVector(5,-2,3)));
    const FTransform Parent=Visual->GetComponentTransform();
    const FTransform Left=Driver->GetHandFrame(true),Right=Driver->GetHandFrame(false);
    TestTrue(TEXT("Action preserves left local rotation through a rotated visual parent"),
        Left.GetRotation().Equals((Parent.GetRotation()*Visual->LeftHandRotation.Quaternion()).GetNormalized(),.0001));
    TestTrue(TEXT("Action preserves right local rotation through a rotated visual parent"),
        Right.GetRotation().Equals((Parent.GetRotation()*Visual->RightHandRotation.Quaternion()).GetNormalized(),.0001));
    TestTrue(TEXT("Per-row left Y override changes position without discarding orientation"),Left.GetLocation().Equals(Parent.TransformPosition(FVector(-22,-31,138)),.001));
    TestTrue(TEXT("Per-row right X override retains other authored coordinates"),Right.GetLocation().Equals(Parent.TransformPosition(FVector(23,27,139)),.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookAllyCollision,"MessControl.ActiveSkills.Motion.HookAllyAndWorldBlocker",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookAllyCollision::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;auto* Ally=T.Ally(600);T.Step(.1f);T.AimForward();
    auto* Wall=T.Box(FVector(300,0,10100),FVector(10,200,200));
    auto* Blocked=Cast<AMCHookSkillAction>(T.Prepare<UMCHookSkillEffect>());
    if(!TestNotNull(TEXT("A blocked aim still permits a visible cast and miss reaction"),Blocked)) return false;
    const FVector BlockedAllyStart=Ally->GetActorLocation();
    Blocked->StartCommitted();T.Step(.45f);
    TestFalse(TEXT("The world wall prevents all ally contact"),Blocked->DidContactTarget());
    TestNull(TEXT("A world-blocked head cannot acquire the ally's motion lease"),Blocked->PulledPawn.Get());
    TestTrue(TEXT("The ally remains behind the wall and is never moved by a rejected hook"),Ally->GetActorLocation().Equals(BlockedAllyStart,.1));
    TestTrue(TEXT("The head never emerges on the far side of the wall"),Blocked->GetHookHeadLocation().X<300);
    T.Step(.8f);TestTrue(TEXT("A blocked cast finishes its visible return and recovery"),Blocked->IsFinished());
    Wall->Destroy();
    auto* Hook=Cast<AMCHookSkillAction>(T.Prepare<UMCHookSkillEffect>({{TEXT("PullSpeed"),500.f}}));
    if(!TestNotNull(TEXT("An unobstructed ally is selected despite Visibility-ignoring player meshes"),Hook)) return false;
    const FVector OwnerStart=T.Hero->GetActorLocation();const float Before=FVector::Dist2D(OwnerStart,Ally->GetActorLocation());
    Hook->StartCommitted();
    TestEqual(TEXT("Contact is preceded by a real anticipation phase"),Hook->GetCastPhase(),EMCHookCastPhase::Windup);
    TestNull(TEXT("No ally is pulled at button press"),Hook->PulledPawn.Get());
    T.Step(.2f);
    TestEqual(TEXT("After anticipation the head is in flight"),Hook->GetCastPhase(),EMCHookCastPhase::Outbound);
    TestNull(TEXT("The ally remains unclaimed while the hook travels"),Hook->PulledPawn.Get());
    TestTrue(TEXT("The flying head separates from a stable caster glove"),FVector::Distance(Hook->GetTetherStart(),Hook->GetHookHeadLocation())>30);
    TestTrue(TEXT("The visible glove remains exactly the cable's caster endpoint"),Hook->GetCasterHandFrame().GetLocation().Equals(Hook->GetTetherStart(),.001));
    T.Step(.3f);
    TestEqual(TEXT("An ally hit pulls that ally, not the caster"),Hook->PulledPawn.Get(),Ally);
    T.Step(.2f);
    TestTrue(TEXT("Native collision movement brings the ally closer"),FVector::Dist2D(T.Hero->GetActorLocation(),Ally->GetActorLocation())<Before-40.f);
    TestTrue(TEXT("The caster is not teleported by the ally branch"),FVector::Dist2D(T.Hero->GetActorLocation(),OwnerStart)<3.f);
    Wall=T.Box((Hook->GetTetherStart()+Hook->GetTetherEnd())*.5f,FVector(10,200,200));
    T.Step(.05f);TestEqual(TEXT("A new wall crossing releases the pull and visibly reels the head"),Hook->GetCastPhase(),EMCHookCastPhase::Returning);
    TestTrue(TEXT("The physical wall releases the movement lease immediately"),MCActiveSkillMotion::CanAcquire(Ally));
    TestTrue(TEXT("Wall stop reason is native telemetry"),Hook->GetPullStopReason()==TEXT("TetherBlocked") || Hook->GetPullStopReason()==TEXT("PullStepBlocked"));
    TestTrue(TEXT("Cancellation releases the ally's temporary flight"),Ally->GetCharacterMovement()->MovementMode!=MOVE_Flying);
    T.Step(.8f);TestTrue(TEXT("The blocked cast finishes only after return/recovery"),Hook->IsFinished());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookBossFamilies,"MessControl.ActiveSkills.Motion.HookBothBossFamiliesKeepsBossStationary",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookBossFamilies::RunTest(const FString&)
{
    for(bool Nut:{false,true}) {
        MCActiveSkillMotionTests::FWorld T;AActor* Target=nullptr;
        const FVector At(600,0,10112);
        if(Nut) {
            // Default Falling BeginPlay moves a boss to its entrance LockedStart.
            // Seed this stationary Idle fixture before BeginPlay, as the native
            // encounter spawn does; an after-spawn state edit leaves it at(0,0,0).
            const FTransform Pose(FRotator::ZeroRotator,At);
            auto* Boss=T.World->SpawnActorDeferred<AMCNutBoss>(AMCNutBoss::StaticClass(),Pose);
            Boss->State=EMCNutBossState::Idle;Boss->Health=100;Boss->FinishSpawning(Pose);Target=Boss;
            TestTrue(TEXT("The native nut boss fixture remains at its intended camera-ray target"),Boss->GetActorLocation().Equals(At,.1));
        } else {
            auto* Boss=T.World->SpawnActor<AMCBossCharacter>(At,FRotator::ZeroRotator);
            Boss->Runtime.State=EMCBossState::Searching;Boss->Runtime.Health=100;Boss->GetCharacterMovement()->DisableMovement();Target=Boss;
        }
        Target->SetActorTickEnabled(false);T.AimForward();
        auto* Hook=Cast<AMCHookSkillAction>(T.Prepare<UMCHookSkillEffect>({{TEXT("PullSpeed"),500.f}}));
        if(!TestNotNull(Nut?TEXT("Nut boss actual body is hookable"):TEXT("Generic boss actual torso is hookable"),Hook)) return false;
        const FVector BossStart=Target->GetActorLocation(),OwnerStart=T.Hero->GetActorLocation();
        Hook->StartCommitted();
        TestNull(TEXT("Boss aim does not create an instantaneous pull"),Hook->PulledPawn.Get());
        T.Step(.65f);
        TestEqual(TEXT("Actual boss contact moves the caster"),Hook->PulledPawn.Get(),T.Hero);
        TestTrue(TEXT("The caster advances through CMC toward the boss"),T.Hero->GetActorLocation().X>OwnerStart.X+40);
        TestTrue(TEXT("Hook never moves the boss transform"),Target->GetActorLocation().Equals(BossStart,.1));
        Target->Destroy();T.Step(.05f);TestEqual(TEXT("A missing target releases movement and reels back"),Hook->GetCastPhase(),EMCHookCastPhase::Returning);
        TestTrue(TEXT("The caster no longer owns a temporary flight mode"),T.Move->MovementMode!=MOVE_Flying);
        TestTrue(TEXT("A missing target cannot retain the movement lease"),MCActiveSkillMotion::CanAcquire(T.Hero));
        T.Step(.8f);TestTrue(TEXT("Target loss completes its visible return"),Hook->IsFinished());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookSupportWalkingControl,"MessControl.ActiveSkills.Motion.HookSupportFixtureOrdinaryWalkingControl",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookSupportWalkingControl::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;T.FloorActor->Destroy();
    auto* A=T.World->SpawnActor<AActor>();auto* Surface=NewObject<UProceduralMeshComponent>(A);A->SetRootComponent(Surface);
    Surface->bUseComplexAsSimpleCollision=true;Surface->bUseAsyncCooking=false;Surface->SetCollisionProfileName(TEXT("BlockAll"));
    Surface->CanCharacterStepUpOn=ECB_Yes;Surface->RegisterComponent();A->SetActorLocation(FVector(0,0,10000));
    MCActiveSkillMotionTests::BuildHookSupport(Surface,false,false);T.Move->bForceNextFloorCheck=true;T.Step(.1f);
    if(!TestTrue(TEXT("Control pawn starts on the exact authored Hook support"),T.Move->IsMovingOnGround()
        && T.Move->CurrentFloor.HitResult.GetComponent()==Surface)) return false;
    const FVector Before=T.Hero->GetActorLocation();
    // Ordinary movement input, without a skill, root-motion source or flight.
    // Force only bypasses an unrelated editor input lock in this native world.
    for(int32 I=0;I<36;++I) {T.Hero->AddMovementInput(FVector::ForwardVector,1.f,true);T.Step(1.f/60);}
    AddInfo(FString::Printf(TEXT("Hook support ordinary walk: before=%s after=%s velocity=%s mode=%d floor=%s normal=%s"),
        *Before.ToString(),*T.Hero->GetActorLocation().ToString(),*T.Move->Velocity.ToString(),int32(T.Move->MovementMode),
        *GetPathNameSafe(T.Move->CurrentFloor.HitResult.GetComponent()),*T.Move->CurrentFloor.HitResult.ImpactNormal.ToString()));
    TestTrue(TEXT("Ordinary native CMC crosses the same rise while grounded"),T.Hero->GetActorLocation().X>Before.X+70 && T.Move->IsMovingOnGround());
    TestTrue(TEXT("Ordinary walking gains the authored support height"),T.Hero->GetActorLocation().Z>Before.Z+5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookGroundedSupport,"MessControl.ActiveSkills.Motion.HookGroundPullFollowsSupportWithoutIgnoringSameComponentWalls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookGroundedSupport::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;T.FloorActor->Destroy();
    auto* A=T.World->SpawnActor<AActor>();auto* Surface=NewObject<UProceduralMeshComponent>(A);A->SetRootComponent(Surface);
    Surface->bUseComplexAsSimpleCollision=true;Surface->bUseAsyncCooking=false;Surface->SetCollisionProfileName(TEXT("BlockAll"));
    Surface->CanCharacterStepUpOn=ECB_Yes;Surface->RegisterComponent();A->SetActorLocation(FVector(0,0,10000));
    auto Build=[&](bool Wall,bool Ceiling) {
        MCActiveSkillMotionTests::BuildHookSupport(Surface,Wall,Ceiling);
        T.Move->bForceNextFloorCheck=true;T.Step(.05f);
    };
    Build(false,false);const FVector Before=T.Hero->GetActorLocation(),Velocity(850,0,0);
    if(!TestTrue(TEXT("The fixture has native walkable support"),T.Move->IsMovingOnGround() && T.Move->CurrentFloor.HitResult.GetComponent()==Surface)) return false;
    TestTrue(TEXT("A shallow rising tongue remains eligible for native grounded pulling"),MCActiveSkillMotion::ClearPullStep(T.Hero,nullptr,nullptr,Velocity,.05f));
    auto* Foreign=T.Box(FVector(70,0,10080),FVector(3,150,80));
    TestFalse(TEXT("A foreign wall still blocks the entire corrected corridor"),MCActiveSkillMotion::ClearPullStep(T.Hero,nullptr,nullptr,Velocity,.05f));Foreign->Destroy();
    Build(true,false);TestFalse(TEXT("A wall on the same tongue component is never ignored"),MCActiveSkillMotion::ClearPullStep(T.Hero,nullptr,nullptr,Velocity,.05f));
    Build(false,true);
    FCollisionQueryParams RoofQuery(SCENE_QUERY_STAT(MCHookRoofWitness),true,T.Hero);RoofQuery.bReturnFaceIndex=true;FHitResult Roof;
    const auto* Capsule=T.Hero->GetCapsuleComponent();
    const FVector RoofStart=T.Hero->GetActorLocation(),RoofEnd=RoofStart+Velocity*.05f;
    // The full sweep can hit floor first. Independently witness the roof at
    // the NEXT capsule's X with an upward ray through its actual underside.
    const bool RoofBlocks=Surface->LineTraceComponent(Roof,FVector(RoofEnd.X,RoofEnd.Y,10100),
        FVector(RoofEnd.X,RoofEnd.Y,10120),RoofQuery);
    AddInfo(FString::Printf(TEXT("Hook roof witness: blocking=%d component=%s normal=%s center=%s end=%s"),
        RoofBlocks,*GetPathNameSafe(Roof.GetComponent()),*Roof.ImpactNormal.ToString(),*RoofStart.ToString(),*RoofEnd.ToString()));
    TestTrue(TEXT("Actual next-step capsule really intersects the same-component roof"),RoofBlocks && Roof.GetComponent()==Surface
        && Roof.ImpactNormal.Z<-.7 && Roof.ImpactPoint.Z<RoofEnd.Z+Capsule->GetScaledCapsuleHalfHeight()-2
        && Roof.ImpactPoint.Z>RoofEnd.Z);
    TestFalse(TEXT("A low same-component ceiling cannot be skipped by query correction"),MCActiveSkillMotion::ClearPullStep(T.Hero,nullptr,nullptr,Velocity,.05f));
    Build(false,false);TestTrue(TEXT("Query correction never moves the physical capsule"),T.Hero->GetActorLocation().Equals(Before,.5));
    // Exercise the real cast handoff, not an unattached lease competing with
    // ordinary tool/grip work that an accepted skill suspends in production.
    auto* Boss=T.World->SpawnActor<AMCBossCharacter>(FVector(350,0,10136),FRotator::ZeroRotator);
    Boss->Runtime.State=EMCBossState::Searching;Boss->Runtime.Health=100;Boss->GetCharacterMovement()->DisableMovement();Boss->SetActorTickEnabled(false);
    T.AimForward();
    auto* Hook=Cast<AMCHookSkillAction>(T.Prepare<UMCHookSkillEffect>());
    if(!TestNotNull(TEXT("A real Hook cast is accepted on the rising support"),Hook)) return false;
    Hook->StartCommitted();T.Step(1.f);
    AddInfo(FString::Printf(TEXT("Hook grounded movement: before=%s after=%s velocity=%s mode=%d floor=%s normal=%s contact=%d phase=%d stop=%s"),
        *Before.ToString(),*T.Hero->GetActorLocation().ToString(),*T.Move->Velocity.ToString(),int32(T.Move->MovementMode),
        *GetPathNameSafe(T.Move->CurrentFloor.HitResult.GetComponent()),*T.Move->CurrentFloor.HitResult.ImpactNormal.ToString(),
        Hook->DidContactTarget(),int32(Hook->GetCastPhase()),*Hook->GetPullStopReason().ToString()));
    TestTrue(TEXT("The rising-support cast actually contacts its native boss"),Hook->DidContactTarget());
    TestTrue(TEXT("The capsule actually advances across the rise and remains grounded"),T.Hero->GetActorLocation().X>Before.X+70 && T.Move->IsMovingOnGround());
    TestTrue(TEXT("Native floor handling lifts the pawn along the real slope"),T.Hero->GetActorLocation().Z>Before.Z+5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCHookMissAndCancel,"MessControl.ActiveSkills.Motion.HookMissReturnsAndWindupCancelOwnsNoMotion",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCHookMissAndCancel::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;T.AimForward();
    auto* Canceled=Cast<AMCHookSkillAction>(T.Prepare<UMCHookSkillEffect>());
    if(!TestNotNull(TEXT("An empty aim is a valid cast"),Canceled)) return false;
    Canceled->StartCommitted();T.Step(.08f);
    TestEqual(TEXT("The first half of anticipation is still winding up"),Canceled->GetCastPhase(),EMCHookCastPhase::Windup);
    TestTrue(TEXT("Anticipation never acquires the caster movement mode"),MCActiveSkillMotion::CanAcquire(T.Hero));
    Canceled->Cancel(EMCActiveSkillEndReason::InputCanceled);
    TestTrue(TEXT("Cancel during anticipation releases the presentation immediately"),Canceled->IsFinished() && !T.Hero->GetActiveSkillPresentation());
    auto* Miss=Cast<AMCHookSkillAction>(T.Prepare<UMCHookSkillEffect>({{TEXT("HookRange"),800.f}}));
    if(!TestNotNull(TEXT("A new miss gets an independent action"),Miss)) return false;
    const FVector OwnerStart=T.Hero->GetActorLocation();Miss->StartCommitted();T.Step(.32f);
    TestEqual(TEXT("The empty cast visibly travels before reeling"),Miss->GetCastPhase(),EMCHookCastPhase::Outbound);
    TestTrue(TEXT("Flying prop advances while the mitten stays near the body"),Miss->GetHookHeadLocation().X>Miss->GetTetherStart().X+150
        && FVector::Distance(Miss->GetTetherStart(),OwnerStart)<140);
    const FRotator Facing=Miss->GetCastFacing();T.Controller->SetControlRotation(FRotator(0,150,0));T.Step(.04f);
    TestTrue(TEXT("Later camera orbit cannot redirect the accepted cast"),Miss->GetCastFacing().Equals(Facing,.001));
    T.Step(.22f);
    TestEqual(TEXT("A range miss enters a finite return phase"),Miss->GetCastPhase(),EMCHookCastPhase::Returning);
    TestFalse(TEXT("No target contact was fabricated at maximum range"),Miss->DidContactTarget());
    TestNull(TEXT("A miss never starts pawn movement"),Miss->PulledPawn.Get());
    T.Step(.65f);
    TestTrue(TEXT("Return and recovery finish without leaking the presentation"),Miss->IsFinished() && !T.Hero->GetActiveSkillPresentation());
    TestTrue(TEXT("A miss preserves the caster's grounded mode and position"),T.Move->IsMovingOnGround() && T.Hero->GetActorLocation().Equals(OwnerStart,.1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCDriverAllyContact,"MessControl.ActiveSkills.Motion.DriverOneHitPerContactEpisode",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCDriverAllyContact::RunTest(const FString&)
{
    MCActiveSkillMotionTests::FWorld T;auto* Ally=T.Ally(220);T.Step(.1f);
    const float AllyBefore=Ally->Status->State.Health,OwnerBefore=T.Hero->Status->State.Health;
    auto* Driver=T.Prepare<UMCDriverSkillEffect>({{TEXT("RideSpeed"),200.f},{TEXT("AllyDamage"),8.f},{TEXT("AllyKnockback"),0.f},{TEXT("AllyLift"),0.f}});
    if(!TestNotNull(TEXT("Driver can summon with another player in its contact lane"),Driver)) return false;
    Driver->StartCommitted();T.Step(.45f);
    TestEqual(TEXT("An overlapping contact episode applies native ally damage once"),Ally->Status->State.Health,AllyBefore-8.f);
    TestEqual(TEXT("The rider is excluded from the run-over ledger"),T.Hero->Status->State.Health,OwnerBefore);
    Driver->Cancel(EMCActiveSkillEndReason::InputCanceled);
    return true;
}
#endif
