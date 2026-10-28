#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MCDataAssets.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCLocomotionSurface.h"
#include "MCSlipperyFood.h"
#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCBrushContactComponent.h"
#include "MCInventoryComponent.h"
#include "MCMouthSurface.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace MCSlipperyFallRuntimeTests
{
constexpr float StepSeconds=1.f/60.f;

/** Only process-local values change; their original values/priority are restored on every exit. */
struct FScopedSlipControls
{
    struct FValue { IConsoleVariable* Variable=nullptr;FString Original;EConsoleVariableFlags Priority=ECVF_SetByConstructor; };
    TArray<FValue> Saved;
    bool bApplied=true;
    FScopedSlipControls(int32 EmptyHandsMode,float Chance)
    {
        Set(TEXT("MC.Slip.EmptyHandsMode"),FString::FromInt(EmptyHandsMode));
        Set(TEXT("MC.Slip.FirstRiskSeconds"),TEXT("1.5"));
        Set(TEXT("MC.Slip.RepeatRiskSeconds"),TEXT("3"));
        Set(TEXT("MC.Slip.FallChance"),FString::SanitizeFloat(Chance));
    }
    ~FScopedSlipControls() {for(int32 I=Saved.Num()-1;I>=0;--I) Saved[I].Variable->Set(*Saved[I].Original,Saved[I].Priority);}
    bool Ready() const {return Saved.Num()==4 && bApplied;}
    bool Set(const TCHAR* Name,const FString& Value)
    {
        if(auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name)) {
            const auto Priority=static_cast<EConsoleVariableFlags>(Variable->GetFlags()&ECVF_SetByMask);
            if(!Saved.ContainsByPredicate([Variable](const FValue& Old){return Old.Variable==Variable;})) Saved.Add({Variable,Variable->GetString(),Priority});
            // UE 5.8 rejects an implicit ReplaceCurrent at constructor priority.
            // Explicitly replace only the existing priority, then prove the effective value.
            Variable->Set(*Value,Priority);
            const bool Applied=FMath::IsNearlyEqual(Variable->GetFloat(),FCString::Atof(*Value),.00001f);
            bApplied&=Applied;return Applied;
        }
        bApplied=false;return false;
    }
};

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
    AMCLocomotionSurface* MakeSurface(EMCGroundSurface Kind,int32 Priority=5000)
    {
        auto* Surface=World->SpawnActor<AMCLocomotionSurface>(FVector(0,0,10020),FRotator::ZeroRotator);
        Surface->HalfExtent=FVector(2800,2800,250);Surface->Surface=Kind;Surface->Priority=Priority;Surface->RefreshBounds();
        return Surface;
    }
    bool DriveUntilFall(int32 Frames)
    {
        for(int32 I=0;I<Frames;++I) {
            if(!Tick(1,true)) return false;
            if(Hero->ToothPhysics->IsSlipping()) return true;
        }
        return false;
    }
    AMCFoodActor* MakeDeliveryFood()
    {
        const FTransform Pose(Hero->GetActorLocation()+FVector(95,0,-25));
        auto* Food=World->SpawnActorDeferred<AMCFoodActor>(AMCFoodActor::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FMCFoodRow Row;Row.Kind=EMCFoodKind::Food;
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        Row.WholeMeshes={Mesh};Row.FragmentMeshes={Mesh};Row.Scale=Row.FragmentScale=FVector(.24f);
        Row.Mass=3.f;Row.SpoilSeconds=300.f;FRandomStream Random(27);
        Food->ConfigureItem(TEXT("SlipRuntimeDelivery"),Row,Random,true);Food->FinishSpawning(Pose);return Food;
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
    FScopedSlipControls Controls(0,1.f);
    if(!TestTrue(TEXT("Legacy severe-skid mode can be scoped independently of risk defaults"),Controls.Ready())) return false;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyMovingRisk,
    "MessControl.Animation.AuthoredGlide.Runtime.StraightMotionUsesRiskAndRetainsLegacyMode",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyMovingRisk::RunTest(const FString&)
{
    using namespace MCSlipperyFallRuntimeTests;
    {
        FScopedSlipControls Controls(1,1.f);FWorldFixture T;
        if(!TestTrue(TEXT("The native risk controls and real supported actor are available"),Controls.Ready() && T.Ready())) return false;
        T.MakeSurface(EMCGroundSurface::Slippery);const double Started=T.World->GetTimeSeconds();
        if(!TestTrue(TEXT("A real straight run advances before the first opportunity"),T.Tick(85,true))) return false;
        TestEqual(TEXT("Movement discovers the actual slippery override"),T.Movement()->GroundSurface,EMCGroundSurface::Slippery);
        TestTrue(TEXT("The runup is actual movement above the risk threshold"),T.Hero->GetVelocity().Size2D()>20.f);
        TestFalse(TEXT("Empty collection state is not a food load"),MCSlipperyFood::IsCarrying(T.Hero.Get()));
        TestEqual(TEXT("Risk never falls before the initial 1.5 moving seconds"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
        if(!TestTrue(TEXT("Forced success takes the native fall transition during straight motion"),T.DriveUntilFall(90))) return false;
        TestTrue(TEXT("The first real fall respects its minimum exposure"),T.World->GetTimeSeconds()-Started>=1.5-StepSeconds);
        TestTrue(TEXT("The first real fall is not delayed behind the old severe-skid condition"),T.World->GetTimeSeconds()-Started<2.8);
        TestTrue(TEXT("The actual risk path retains finite motion"),T.bFiniteThroughout);
    }
    {
        FScopedSlipControls Controls(0,1.f);FWorldFixture T;
        if(!TestTrue(TEXT("The comparison actor has the same real floor and clips"),Controls.Ready() && T.Ready())) return false;
        T.MakeSurface(EMCGroundSurface::Slippery);
        if(!TestTrue(TEXT("The selectable legacy mode runs the same straight input"),T.Tick(180,true))) return false;
        TestEqual(TEXT("The comparison is still on a genuinely slippery surface"),T.Movement()->GroundSurface,EMCGroundSurface::Slippery);
        TestTrue(TEXT("Legacy comparison is moving, rather than avoiding the candidate by stopping"),T.Hero->GetVelocity().Size2D()>20.f);
        TestEqual(TEXT("Straight legacy motion has no severe skid and does not fall"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
        TestTrue(TEXT("The legacy comparison remains actionable"),T.Hero->ToothPhysics->CanAct());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyRepeatRisk,
    "MessControl.Animation.AuthoredGlide.Runtime.FailedRiskWaitsForRepeatWindow",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyRepeatRisk::RunTest(const FString&)
{
    using namespace MCSlipperyFallRuntimeTests;
    FScopedSlipControls Controls(1,0.f);FWorldFixture T;
    if(!TestTrue(TEXT("Repeat risk fixture uses registered controls and the real actionable actor"),Controls.Ready() && T.Ready())) return false;
    T.MakeSurface(EMCGroundSurface::Slippery);const float Health=T.Hero->Status->State.Health;
    if(!TestTrue(TEXT("Zero chance crosses the first native risk window with actual movement"),T.Tick(108,true))) return false;
    TestEqual(TEXT("A failed first opportunity preserves Standing"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
    TestTrue(TEXT("The first risk window was spent while actually moving on slippery ground"),T.Movement()->GroundSurface==EMCGroundSurface::Slippery && T.Hero->GetVelocity().Size2D()>20.f);
    if(!TestTrue(TEXT("The effective native chance really becomes one for the next opportunity"),Controls.Set(TEXT("MC.Slip.FallChance"),TEXT("1")))) return false;
    if(!TestTrue(TEXT("Changing chance after a failed opportunity continues real movement"),T.Tick(120,true))) return false;
    TestEqual(TEXT("The risk is not rerolled each frame after chance becomes one"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
    if(!TestTrue(TEXT("The next native opportunity eventually accepts the forced-success outcome"),T.DriveUntilFall(100))) return false;
    TestEqual(TEXT("Neither failed risk nor ordinary fall damages health"),T.Hero->Status->State.Health,Health);
    TestTrue(TEXT("Repeat-window movement remains finite"),T.bFiniteThroughout);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyNormalFooting,
    "MessControl.Animation.AuthoredGlide.Runtime.NormalFloorAndNormalOverrideDoNotProduceFalls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyNormalFooting::RunTest(const FString&)
{
    using namespace MCSlipperyFallRuntimeTests;
    FScopedSlipControls Controls(1,1.f);FWorldFixture T;
    if(!TestTrue(TEXT("Normal footing fixture begins with a real walking actor"),Controls.Ready() && T.Ready())) return false;
    if(!TestTrue(TEXT("Ordinary collision floor supports more than one first-risk window"),T.Tick(150,true))) return false;
    TestEqual(TEXT("Native footing detection remains ordinary on the unmodified floor"),T.Movement()->GroundSurface,EMCGroundSurface::Normal);
    TestEqual(TEXT("Ordinary walking never enters the actual fall"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
    T.MakeSurface(EMCGroundSurface::Slippery);T.MakeSurface(EMCGroundSurface::Normal,6000);
    if(!TestTrue(TEXT("A real authored normal override wins over the overlapping slippery volume"),T.Tick(150,true))) return false;
    TestEqual(TEXT("Surface selection actually resolves the higher-priority normal volume"),T.Movement()->GroundSurface,EMCGroundSurface::Normal);
    TestEqual(TEXT("Normal override movement produces no slippery fall"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
    TestTrue(TEXT("Movement is real and actionable under the normal override"),T.Hero->GetVelocity().Size2D()>20.f && T.Hero->ToothPhysics->CanAct());
    // This is the authored normal-volume contract; real IsSafePoint ice-circle
    // geometry and its final footing override remain covered by MCIceEventTests.
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyCarriedDelivery,
    "MessControl.Animation.AuthoredGlide.Runtime.CarriedDeliveryFallsDropsAndRecoversWithoutDamage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyCarriedDelivery::RunTest(const FString&)
{
    using namespace MCSlipperyFallRuntimeTests;
    FScopedSlipControls Controls(0,0.f);FWorldFixture T;
    if(!TestTrue(TEXT("Carry fixture has the native standing actor"),Controls.Ready() && T.Ready())) return false;
    auto* Food=T.MakeDeliveryFood();auto* Collection=T.Hero->FoodCollection.Get();Collection->bCollecting=true;
    if(!TestTrue(TEXT("The real collection endpoint accepts an ordinary delivery fragment"),Collection->Collect(Food))) return false;
    TestTrue(TEXT("Native pickup owns and classifies this actual food load"),Food->StackCarrier==T.Hero.Get() && MCSlipperyFood::IsCarrying(T.Hero.Get()));
    const float Health=T.Hero->Status->State.Health;T.MakeSurface(EMCGroundSurface::Slippery);
    if(!TestTrue(TEXT("A moving carrier takes the native fall even with zero random chance and legacy empty-hand mode"),T.DriveUntilFall(50))) return false;
    TestTrue(TEXT("The fall clears the actual delivery collection"),Collection->Pieces.IsEmpty() && !Collection->bCollecting);
    TestTrue(TEXT("The original food actor is physically released rather than erased"),IsValid(Food) && !Food->StackCarrier && Food->Phase==EMCFoodPhase::Free);
    TestFalse(TEXT("A released food actor is no longer the hero's delivery load"),MCSlipperyFood::IsCarrying(T.Hero.Get()));
    bool Recovered=false;
    for(int32 I=0;I<360;++I) {
        if(!T.Tick(1)) return TestTrue(TEXT("The actual carrier survives the full fall/recovery"),false);
        if(T.Hero->ToothPhysics->GetBodyState()==EMCBodyState::Standing) {Recovered=true;break;}
    }
    TestTrue(TEXT("Actual carry failure returns control through the normal recovery"),Recovered && T.Hero->ToothPhysics->CanAct());
    TestEqual(TEXT("Carried-food failure and recovery do not remove HP"),T.Hero->Status->State.Health,Health);
    TestTrue(TEXT("The real fall, delivery release and recovery remain finite"),T.bFiniteThroughout);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCSlipperyBrushReadiness,
    "MessControl.Animation.AuthoredGlide.Runtime.WorkingBrushProtectsButEmptyPrimaryDoesNot",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMCSlipperyBrushReadiness::RunTest(const FString&)
{
    using namespace MCSlipperyFallRuntimeTests;
    FScopedSlipControls Controls(1,1.f);FWorldFixture T;
    if(!TestTrue(TEXT("Brush readiness fixture has the real current character"),Controls.Ready() && T.Ready())) return false;
    T.MakeSurface(EMCGroundSurface::Slippery);
    const FTransform Pose(FVector(0,0,10023));
    auto* Patch=T.World->SpawnActorDeferred<AMCMouthSurface>(AMCMouthSurface::StaticClass(),Pose);
    Patch->bRandomizeLiquidSize=false;Patch->LiquidHalfSize=1600;Patch->GroundResponse=EMCGroundSurface::Slippery;
    Patch->FinishSpawning(Pose);Patch->Status->ApplyCoffee(1.f);
    T.Hero->Inventory->ServerSelect(EMCToolSlot::Brush);T.Hero->SetPrimaryInputHeld(true);
    const float Health=T.Hero->Status->State.Health,InitialWet=Patch->RemainingLiquid();
    const FVector Started=T.Hero->GetActorLocation();int32 ReadyMovingFrames=0;
    // A deliberately slow, real analog walk follows the acquired floor contact.
    // Full 340cm/s movement outruns the native 180cm/s brush target interpolation,
    // repeatedly putting the old point behind the wrist before its approach finishes.
    for(int32 I=0;I<360 && ReadyMovingFrames<100;++I) {
        T.Hero->AddMovementInput(FVector::ForwardVector,.18f);
        if(!T.Tick(1)) return TestTrue(TEXT("The actual moving brush worker survives readiness"),false);
        if(T.Hero->BrushContact->IsWorkReady() && T.Hero->GetVelocity().Size2D()>20.f) ++ReadyMovingFrames;
        if(T.Hero->ToothPhysics->IsSlipping()) break;
    }
    AddInfo(FString::Printf(TEXT("Brush fixture: ready_moving_frames=%d travel_cm=%.1f speed=%.1f wet=%.5f/%.5f primary=%d brushing=%d slipping=%d"),
        ReadyMovingFrames,FVector::Dist2D(Started,T.Hero->GetActorLocation()),T.Hero->GetVelocity().Size2D(),Patch->RemainingLiquid(),InitialWet,
        T.Hero->IsPrimaryHeld(),T.Hero->bBrushing,T.Hero->ToothPhysics->IsSlipping()));
    if(!TestTrue(TEXT("Protection is based on over 1.5 seconds of real IsWorkReady contact during movement"),ReadyMovingFrames>=100)) return false;
    TestTrue(TEXT("The trajectory contains actual movement rather than a standing brush fixture"),FVector::Dist2D(Started,T.Hero->GetActorLocation())>20.f);
    TestTrue(TEXT("Native working contacts genuinely remove dirt without clearing the complete test area"),Patch->RemainingLiquid()<InitialWet && Patch->RemainingLiquid()>0.f);
    TestEqual(TEXT("Real working contact keeps a moving worker standing on slippery ground"),T.Hero->ToothPhysics->GetBodyState(),EMCBodyState::Standing);
    Patch->ClearLiquidForBooster();
    TestTrue(TEXT("The input remains held when its real dirty target is removed"),T.Hero->IsPrimaryHeld());
    if(!TestTrue(TEXT("Holding primary in empty space does not prevent the next native risk fall"),T.DriveUntilFall(180))) return false;
    TestEqual(TEXT("Neither working protection nor later empty-primary fall changes HP"),T.Hero->Status->State.Health,Health);
    TestTrue(TEXT("The brush readiness regression retains finite native motion"),T.bFiniteThroughout);
    return true;
}
#endif
