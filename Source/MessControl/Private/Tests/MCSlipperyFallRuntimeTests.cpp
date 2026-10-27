#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCDataAssets.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCLocomotionSurface.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace MCSlipperyFallRuntimeTests
{
constexpr float StepSeconds=1.f/60.f;

struct FWorldFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    TWeakObjectPtr<AMCToothCharacter> Hero;
    TWeakObjectPtr<UBoxComponent> Floor;
    TStrongObjectPtr<UMCAnimationProfile> Profile;
    bool bFiniteThroughout=true;

    FWorldFixture()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));
        World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        auto* FloorActor=World->SpawnActor<AActor>();
        auto* Shape=NewObject<UBoxComponent>(FloorActor);Floor=Shape;
        FloorActor->SetRootComponent(Shape);Shape->SetBoxExtent(FVector(3000,3000,20));
        Shape->SetCollisionProfileName(TEXT("BlockAll"));Shape->SetCollisionObjectType(ECC_WorldStatic);
        Shape->RegisterComponent();FloorActor->SetActorLocation(FVector(0,0,10000));

        // Only the transient duplicate is changed; saved profile/CDO/assets stay untouched.
        const auto* Source=LoadObject<UMCAnimationProfile>(nullptr,TEXT("/Game/Data/DA_ToothAnimation.DA_ToothAnimation"));
        Profile.Reset(Source?DuplicateObject<UMCAnimationProfile>(Source,GetTransientPackage()):NewObject<UMCAnimationProfile>());
        Profile->SetFlags(RF_Transient);
        Profile->Glide=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Gameplay/CharacterCurrent/Animations/Glide/A_Teeth_Glide1.A_Teeth_Glide1"));
        Profile->GlideStrong=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Gameplay/CharacterCurrent/Animations/Glide/A_Teeth_Glide2.A_Teeth_Glide2"));
        const FTransform Spawn(FVector(0,0,10080));
        auto* Pawn=World->SpawnActorDeferred<AMCToothCharacter>(AMCToothCharacter::StaticClass(),Spawn,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Hero=Pawn;Pawn->AnimationProfile=Profile.Get();Pawn->FinishSpawning(Spawn);
        auto* Controller=World->SpawnActor<APlayerController>();
        Controller->SetAsLocalPlayerController();Controller->Possess(Pawn);
        Pawn->Status->Initialize(100);
        Tick(15);
    }
    ~FWorldFixture()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    UMCToothMovementComponent* Movement() const
    { return Hero.IsValid()?Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()):nullptr; }
    bool Ready() const
    {
        const auto* Pawn=Hero.Get();const auto* Move=Movement();
        const auto* Mesh=Pawn?Pawn->GetMesh()->GetSkeletalMeshAsset():nullptr;
        return Pawn && Pawn->IsLocallyControlled() && Pawn->GetController() && Pawn->ToothPhysics->CanAct()
            && Move && Move->IsMovingOnGround() && Mesh && Profile->Glide && Profile->GlideStrong
            && Profile->GlideStrong->GetSkeleton()==Mesh->GetSkeleton();
    }
    bool Tick(int32 Frames,bool Drive=false)
    {
        for(int32 I=0;I<Frames;++I) {
            if(!Hero.IsValid()) return false;
            if(Drive) Hero->AddMovementInput(FVector::ForwardVector);
            ++GFrameCounter;World->Tick(LEVELTICK_All,StepSeconds);
            if(!Hero.IsValid()) return false;
            bFiniteThroughout&=!Hero->GetActorLocation().ContainsNaN() && !Hero->GetVelocity().ContainsNaN();
        }
        return true;
    }
    bool BeginFall()
    {
        auto* Move=Movement();if(!Move || !Hero.IsValid()) return false;
        // Exercise the production transition and real floor/capsule, independently of the pure skid gate tests.
        Move->GroundSurface=EMCGroundSurface::Slippery;Move->Velocity=FVector(320,0,0);
        return Hero->ToothPhysics->BeginSlipperyFall();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFallLifecycle,
    "MessControl.Animation.AuthoredGlide.Runtime.SupportedFallLifecycleAndGuards",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFallLifecycle::RunTest(const FString&)
{
    using namespace MCSlipperyFallRuntimeTests;
    FWorldFixture T;
    if(!TestTrue(TEXT("The real current hero and imported fall clip stand on a local collision floor"),T.Ready())) return false;
    T.Movement()->GroundSurface=EMCGroundSurface::Normal;
    TestFalse(TEXT("Ordinary ground rejects the actual fall transition"),T.Hero->ToothPhysics->BeginSlipperyFall());
    UAnimSequence* Fall=T.Profile->GlideStrong;T.Profile->GlideStrong=nullptr;
    T.Movement()->GroundSurface=EMCGroundSurface::Slippery;
    TestFalse(TEXT("A missing full fall clip rejects entry instead of locking control"),T.Hero->ToothPhysics->BeginSlipperyFall());
    TestTrue(TEXT("Rejected entry leaves the living character actionable"),T.Hero->ToothPhysics->CanAct());
    T.Profile->GlideStrong=Fall;
    const float Health=T.Hero->Status->State.Health,Duration=float(Fall->GetPlayLength());
    const FVector Start=T.Hero->GetActorLocation();const double Started=T.World->GetTimeSeconds();
    if(!TestTrue(TEXT("A supported slippery fall enters its real replicated body state"),T.BeginFall())) return false;
    TestEqual(TEXT("The new transition is Slipping, not cosmetic balance"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Slipping);
    TestFalse(TEXT("A full fall locks gameplay actions"),T.Hero->ToothPhysics->CanAct());
    T.Movement()->SetMovementMode(MOVE_Walking);
    TestEqual(TEXT("Movement cannot unlock the fall early"),T.Movement()->MovementMode.GetValue(),MOVE_None);
    if(!TestTrue(TEXT("The actor survives the complete authored fall interval"),T.Tick(FMath::Max(1,FMath::FloorToInt((Duration-.05f)/StepSeconds)),true))) return false;
    TestEqual(TEXT("Recovery cannot truncate the imported full fall clip"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Slipping);
    bool SawRecovery=false,Stood=false;
    for(int32 I=0;I<180;++I) {
        if(!TestTrue(TEXT("The fall/recovery actor remains valid"),T.Tick(1))) return false;
        const auto State=T.Hero->ToothPhysics->GetBodyState();
        if(State==EMCBodyState::Recovering && !SawRecovery) {
            SawRecovery=true;
            TestTrue(TEXT("The real recovery starts after the full source clip"),T.World->GetTimeSeconds()-Started>=Duration);
            TestFalse(TEXT("Recovery still owns control"),T.Hero->ToothPhysics->CanAct());
        }
        if(State==EMCBodyState::Standing) {Stood=true;break;}
    }
    TestTrue(TEXT("World ticks pass through real Recovering"),SawRecovery);
    TestTrue(TEXT("The finite fall and rise restore Standing"),Stood);
    TestTrue(TEXT("Control is actionable after rising"),T.Hero->ToothPhysics->CanAct());
    TestTrue(TEXT("Standing restores a ground movement mode"),T.Movement()->IsMovingOnGround());
    TestEqual(TEXT("An ordinary supported slip never removes HP"),T.Hero->Status->State.Health,Health);
    const FVector End=T.Hero->GetActorLocation();
    TestTrue(TEXT("The fall has bounded real capsule slide travel"),End.X-Start.X>10.f && End.X-Start.X<200.f);
    if(!TestTrue(TEXT("The standing actor accepts movement after recovery"),T.Tick(18,true))) return false;
    TestTrue(TEXT("Restored controls move the capsule rather than leaving a deadlock"),T.Hero->GetActorLocation().X>End.X+2.f);
    TestTrue(TEXT("All fall, slide, recovery and resumed positions remain finite"),T.bFiniteThroughout);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFallInterruptions,
    "MessControl.Animation.AuthoredGlide.Runtime.SupportLossAndDeathCannotStand",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFallInterruptions::RunTest(const FString&)
{
    using namespace MCSlipperyFallRuntimeTests;
    {
        FWorldFixture T;
        if(!TestTrue(TEXT("Support-loss fixture has the real actionable hero and fall clip"),T.Ready())
            || !TestTrue(TEXT("Support-loss case starts a real fall"),T.BeginFall())) return false;
        T.Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if(!TestTrue(TEXT("Removing support leaves the pawn valid"),T.Tick(2))) return false;
        TestEqual(TEXT("Lost support immediately becomes native Ragdoll"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
        TestTrue(TEXT("Fallback actually simulates the character body"),T.Hero->GetMesh()->IsSimulatingPhysics(T.Hero->RigBone(TEXT("body"))));
        const double BodyZ=T.Hero->ToothPhysics->PhysicalLocation().Z;
        if(!TestTrue(TEXT("The unsupported physical body continues ticking"),T.Tick(18))) return false;
        TestTrue(TEXT("The actual unsupported body falls under gravity"),T.Hero->ToothPhysics->PhysicalLocation().Z<BodyZ-2.f);
        TestFalse(TEXT("Unsupported ragdoll cannot accept work"),T.Hero->ToothPhysics->CanAct());
        TestTrue(TEXT("Support loss retains finite actor motion"),T.bFiniteThroughout);
    }
    {
        FWorldFixture T;
        if(!TestTrue(TEXT("Death fixture has the real actionable hero and fall clip"),T.Ready())
            || !TestTrue(TEXT("Death case starts a real fall"),T.BeginFall())
            || !TestTrue(TEXT("The authored fall advances before interruption"),T.Tick(12))) return false;
        TestTrue(TEXT("Real lethal status damage interrupts the full fall"),T.Hero->Status->Damage(1000.f,FVector::ForwardVector,nullptr,EMCRogueDamageKind::Ordinary,false));
        if(!TestTrue(TEXT("The dead pawn remains available beyond every authored fall/rise timer"),T.Tick(300))) return false;
        TestFalse(TEXT("The fall timer cannot revive health"),T.Hero->Status->IsAlive());
        TestEqual(TEXT("Death remains in native Ragdoll instead of Standing"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
        TestFalse(TEXT("Death cannot restore actions through recovery"),T.Hero->ToothPhysics->CanAct());
        TestEqual(TEXT("Death cannot restore locomotion"),T.Movement()->MovementMode.GetValue(),MOVE_None);
        TestTrue(TEXT("Death interruption retains finite actor motion"),T.bFiniteThroughout);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFallRejectedRetry,
    "MessControl.Animation.AuthoredGlide.Runtime.BlockedAttemptCanRetryAfterClearance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFallRejectedRetry::RunTest(const FString&)
{
    using namespace MCSlipperyFallRuntimeTests;
    FWorldFixture T;
    if(!TestTrue(TEXT("Retry fixture has the actual current hero and imported fall clip"),T.Ready())) return false;
    const float Health=T.Hero->Status->State.Health;
    auto* Surface=T.World->SpawnActor<AMCLocomotionSurface>(FVector(0,0,10020),FRotator::ZeroRotator);
    Surface->HalfExtent=FVector(2500,2500,250);Surface->Surface=EMCGroundSurface::Slippery;Surface->Priority=5000;Surface->RefreshBounds();
    auto* RoofActor=T.World->SpawnActor<AActor>();
    auto* Roof=NewObject<UBoxComponent>(RoofActor);RoofActor->SetRootComponent(Roof);
    // The standing capsule ends near Z=10138. The authored clearance box ends
    // at Z=10158: this ceiling rejects a fall without blocking ordinary movement.
    Roof->SetBoxExtent(FVector(2500,2500,10));Roof->SetCollisionProfileName(TEXT("BlockAll"));
    Roof->RegisterComponent();RoofActor->SetActorLocation(FVector(0,0,10160));
    auto Drive=[&](FVector Direction,int32 Frames,bool* SawSlip=nullptr) {
        for(int32 I=0;I<Frames;++I) {
            if(!T.Hero.IsValid()) return false;
            T.Hero->AddMovementInput(Direction);
            if(!T.Tick(1)) return false;
            if(SawSlip && T.Hero->ToothPhysics->IsSlipping()) {*SawSlip=true;return true;}
        }
        return true;
    };
    T.Hero->SetSprintInputHeld(true);
    if(!TestTrue(TEXT("The real sprint reaches its supported native gate"),Drive(FVector::ForwardVector,90))) return false;
    TestTrue(TEXT("The runup actually reaches sprint speed"),T.Hero->GetVelocity().X>450.f);
    TestEqual(TEXT("The live override supplies actual slippery footing"),T.Movement()->GroundSurface,EMCGroundSurface::Slippery);
    if(!TestTrue(TEXT("The first real countersteer remains a valid pawn"),Drive(-FVector::ForwardVector,50))) return false;
    TestEqual(TEXT("A real ceiling rejects the severe-skid fall"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
    if(!TestTrue(TEXT("The blocked pawn can accelerate back along the same floor"),Drive(-FVector::ForwardVector,120))) return false;
    TestTrue(TEXT("The second runup is real reverse sprint travel"),T.Hero->GetVelocity().X<-450.f);
    Roof->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bool SawSlip=false;
    if(!TestTrue(TEXT("After removing only the fixture obstacle, a second countersteer runs"),Drive(FVector::ForwardVector,60,&SawSlip))) return false;
    TestTrue(TEXT("A blocked attempt does not consume the 6.5s actual-fall cooldown"),SawSlip);
    TestEqual(TEXT("A rejected attempt and later real slip do not remove HP"),T.Hero->Status->State.Health,Health);
    TestTrue(TEXT("The obstacle retry preserves finite native motion"),T.bFiniteThroughout);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyFallCurvedSupport,
    "MessControl.Animation.AuthoredGlide.Runtime.CurvedSupportKeepsObstaclesAndSupportLoss",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyFallCurvedSupport::RunTest(const FString&)
{
    using namespace MCSlipperyFallRuntimeTests;
    FWorldFixture T;
    if(!TestTrue(TEXT("Curved support fixture has the real hero and imported fall clip"),T.Ready())) return false;
    T.Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    auto* SupportActor=T.World->SpawnActor<AActor>();
    auto* Support=NewObject<UProceduralMeshComponent>(SupportActor);SupportActor->SetRootComponent(Support);
    Support->bUseComplexAsSimpleCollision=true;Support->bUseAsyncCooking=false;
    Support->SetCollisionProfileName(TEXT("BlockAll"));Support->SetCollisionObjectType(ECC_WorldStatic);Support->RegisterComponent();
    SupportActor->SetActorLocation(FVector(0,0,10020));
    // A shallow concave trough has a flat walkable centre, while its raised
    // sides enter the wider reclined tangent box. Slide travel stays along X.
    const TArray<float> Rows={-800.f,-145.f,-100.f,-60.f,-45.f,45.f,60.f,100.f,145.f,800.f};
    TArray<FVector> Vertices,Normals;TArray<int32> Triangles;
    for(const float Y:Rows) {
        const float Z=FMath::Clamp((FMath::Abs(Y)-45.f)*.2f,0.f,20.f);
        Vertices.Add(FVector(-1500,Y,Z));Vertices.Add(FVector(1500,Y,Z));
    }
    for(int32 I=0;I<Rows.Num()-1;++I) {
        const int32 A=I*2;Triangles.Append({A,A+1,A+3,A,A+3,A+2});
    }
    Normals.Init(FVector::UpVector,Vertices.Num());
    Support->CreateMeshSection(0,Vertices,Triangles,Normals,TArray<FVector2D>(),TArray<FColor>(),TArray<FProcMeshTangent>(),true);
    T.Movement()->bForceNextFloorCheck=true;
    if(!TestTrue(TEXT("The actual curved collision supports world ticks"),T.Tick(15))) return false;
    const FHitResult& Floor=T.Movement()->CurrentFloor.HitResult;
    if(!TestTrue(TEXT("Native floor detection selects this exact walkable procedural component"),
        Floor.GetComponent()==Support && T.Movement()->IsWalkable(Floor))) return false;
    const auto* Capsule=T.Hero->GetCapsuleComponent();const float Scale=T.Hero->GetActorScale3D().GetAbsMax();
    const FVector Normal=Floor.ImpactNormal.GetSafeNormal(),Center=Floor.ImpactPoint+Normal*70.f*Scale;
    const FQuat Rotation=FQuat::FindBetweenNormals(FVector::UpVector,Normal)*T.Hero->GetActorQuat();
    TestTrue(TEXT("Actual raised support triangles intersect the unfiltered reclined footprint"),
        T.World->OverlapBlockingTestByChannel(Center,Rotation,Capsule->GetCollisionObjectType(),FCollisionShape::MakeBox(FVector(100,100,68)*Scale),
            FCollisionQueryParams(SCENE_QUERY_STAT(MCSlipCurvedSupport),false,T.Hero.Get()),FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())));
    auto* Roof=NewObject<UBoxComponent>(SupportActor);Roof->SetupAttachment(Support);
    Roof->SetBoxExtent(FVector(250,250,10));Roof->SetCollisionProfileName(TEXT("BlockAll"));Roof->RegisterComponent();
    Roof->SetWorldLocation(FVector(0,0,10160));
    TestFalse(TEXT("A separate blocking component on the same support actor still rejects a fall"),T.BeginFall());
    Roof->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    const FVector Started=T.Hero->GetActorLocation();const float Health=T.Hero->Status->State.Health;
    if(!TestTrue(TEXT("Only the actual curved support is excluded from entry clearance"),T.BeginFall())
        || !TestTrue(TEXT("The supported authored fall advances on real curved collision"),T.Tick(30))) return false;
    TestEqual(TEXT("The shallow curved support retains the real full fall"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Slipping);
    TestTrue(TEXT("The footprint sweep does not stop against its own curved support"),T.Hero->GetActorLocation().X>Started.X+35.f);
    TestEqual(TEXT("The supported curved fall preserves HP"),T.Hero->Status->State.Health,Health);
    Support->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(!TestTrue(TEXT("Removing the exact support still ticks the actual character"),T.Tick(2))) return false;
    TestEqual(TEXT("The original unfiltered floor query still detects support loss"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Ragdoll);
    TestTrue(TEXT("The curved support regression retains finite native motion"),T.bFiniteThroughout);
    return true;
}
#endif
