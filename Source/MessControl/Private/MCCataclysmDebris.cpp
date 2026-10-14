#include "MCCataclysmDebris.h"

#include "MCCataclysmRaft.h"
#include "MCCoffeeFlood.h"
#include "MCGripComponent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr double PlayerContactCooldown = .9;
    constexpr float ContactRetireSeconds = .34f;
    constexpr int32 MaxRememberedPlayers = 32;

    // Shared across the bounded stream, so simultaneous planks cannot repeatedly
    // launch one player. Weak keys never retain a pawn or an old PIE world.
    struct FRecentDebrisContact
    {
        TWeakObjectPtr<AMCToothCharacter> Player;
        double At = 0.;
    };
    TArray<FRecentDebrisContact> RecentContacts;

    bool ClaimPlayerContact(AMCToothCharacter* Player, double Now)
    {
        RecentContacts.RemoveAll([Player, Now](const FRecentDebrisContact& Entry)
        {
            return !Entry.Player.IsValid()
                || (Entry.Player->GetWorld() == Player->GetWorld() && Now - Entry.At > 2.);
        });
        for (FRecentDebrisContact& Entry : RecentContacts)
        {
            if (Entry.Player.Get() != Player) continue;
            if (Now - Entry.At < PlayerContactCooldown) return false;
            Entry.At = Now;
            return true;
        }
        if (RecentContacts.Num() >= MaxRememberedPlayers) RecentContacts.RemoveAt(0);
        RecentContacts.Add({Player, Now});
        return true;
    }
}

AMCCataclysmDebris::AMCCataclysmDebris()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(20.f);
    SetMinNetUpdateFrequency(10.f);
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    Body = CreateDefaultSubobject<UMCCataclysmDebrisMeshComponent>(TEXT("StormPlank"));
    SetRootComponent(Body);
    Body->SetMobility(EComponentMobility::Movable);
    Body->SetSimulatePhysics(false);
    Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Body->SetCollisionObjectType(ECC_WorldDynamic);
    Body->SetCollisionResponseToAllChannels(ECR_Ignore);
    Body->SetGenerateOverlapEvents(false);
    Body->SetCanEverAffectNavigation(false);
    Body->CanCharacterStepUpOn = ECB_No;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) Body->SetStaticMesh(Cube.Object);
    // Existing stock wood texture/material, applied to a tiny 12-triangle hull.
    // Gameplay collision therefore follows the visible solid plank exactly.
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Wood(TEXT(
        "/Game/NiagaraExamples/StaticMesh/Wood_Debris_vb2mfeliw/MI_Wood_Debris_vb2mfeliw_2K.MI_Wood_Debris_vb2mfeliw_2K"));
    if (Wood.Succeeded() && Wood.Object->GetBlendMode() == BLEND_Opaque) Body->SetMaterial(0, Wood.Object);
    Cue = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PlankContactCue"));
    Cue->SetupAttachment(Body);
    Cue->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Cue->SetCastShadow(false);
    Cue->SetCanEverAffectNavigation(false);
    Cue->SetTranslucentSortPriority(3);
    Tags.Add(TEXT("CataclysmDebris"));
}

void AMCCataclysmDebris::BeginPlay()
{
    Super::BeginPlay();
    OnRep_Config();
    // Even an accidentally unconfigured spawn cannot survive the stream owner.
    if (HasAuthority()) SetLifeSpan(FMath::Clamp(LifetimeSeconds, 1.f, 12.f));
}

double AMCCataclysmDebris::Clock() const
{
    const UWorld* World = GetWorld();
    if (!World) return 0.;
    if (const AGameStateBase* State = World->GetGameState()) return State->GetServerWorldTimeSeconds();
    return World->GetTimeSeconds();
}

void AMCCataclysmDebris::ConfigureBody()
{
    FRandomStream Random(Seed);
    HalfExtent.X = Random.FRandRange(65.f, 85.f);
    HalfExtent.Y = Random.FRandRange(25.f, 32.f);
    HalfExtent.Z = Random.FRandRange(27.f, 33.f);
    SpinDegrees = Random.FRandRange(-14.f, 14.f);
    SpeedOffset = Random.FRandRange(-25.f, 25.f);
    // Engine cube has a known 50 cm half-extent; no actor/pawn physics is scaled.
    Body->SetRelativeScale3D(HalfExtent / 50.f);
    if (!bCueInitialized && HasActorBegunPlay())
    {
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Cataclysm/Raft/M_RaftCue.M_RaftCue"));
        if (Material) Cue->SetMaterial(0, Material);
        else Cue->SetVisibility(false);
        bCueInitialized = true;
    }
}

void AMCCataclysmDebris::OnRep_Config()
{
    if (TickFlood.IsValid() && TickFlood.Get() != Flood) RemoveTickPrerequisiteActor(TickFlood.Get());
    if (IsValid(Flood) && TickFlood.Get() != Flood)
    {
        AddTickPrerequisiteActor(Flood);
        TickFlood = Flood;
    }
    ConfigureBody();
}

void AMCCataclysmDebris::ConfigureDebris(AMCCoffeeFlood* InFlood, AMCTongue* InTongue, int32 InSeed)
{
    if (!HasAuthority() || !IsValid(InFlood) || !IsValid(InTongue) || !InFlood->IsCataclysmStorm()) return;
    if (!Body->GetStaticMesh()) { StopDebris(); return; }
    Flood = InFlood;
    Tongue = InTongue;
    Seed = InSeed;
    SpawnedAt = Clock();
    RetireAt = -1.;
    OnRep_Config();
    FVector Position = GetActorLocation();
    Position.Z = Flood->SurfaceHeightAt(Position) + HalfExtent.Z * .4f;
    const FVector Direction = Flood->StormStreamDirection().GetSafeNormal2D();
    if (Direction.IsNearlyZero()) { StopDebris(); return; }
    SetActorLocationAndRotation(Position, FRotator(0.f, Direction.Rotation().Yaw + (Seed % 17 - 8), 0.f), false);
    DriftVelocity = Direction * (320.f + SpeedOffset);
    SetLifeSpan(FMath::Clamp(LifetimeSeconds, 1.f, 12.f));
    ForceNetUpdate();
}

void AMCCataclysmDebris::StopDebris()
{
    if (HasAuthority()) Destroy();
}

bool AMCCataclysmDebris::IsWarning() const
{
    return SpawnedAt >= 0. && Clock() - SpawnedAt < FMath::Clamp(WarningSeconds, .7f, 1.f);
}

bool AMCCataclysmDebris::ClearContact(const FHitResult& Hit, FVector Start) const
{
    const FVector Point = Hit.ImpactPoint;
    if (Point.ContainsNaN()) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCCataclysmDebrisSight), false, this);
    Params.AddIgnoredActor(Flood);
    Params.AddIgnoredActor(GetOwner());
    Params.AddIgnoredActor(Hit.GetActor());
    // Use the actual surface contact height, rather than tracing through a deck
    // from the submerged center or applying a hit across a wall/another tooth.
    Start.Z = Point.Z;
    FHitResult Barrier;
    return !GetWorld()->LineTraceSingleByChannel(Barrier, Start, Point, ECC_Visibility, Params);
}

void AMCCataclysmDebris::MarkImpact(double Now, FVector ContactPoint)
{
    ++ImpactCount;
    LastImpactAt = Now;
    RetireAt = Now + ContactRetireSeconds;
    if (IsValid(Flood)) Flood->NotifyStormImpact(ContactPoint, DriftVelocity.GetSafeNormal2D(), .8f);
    DriftVelocity *= .15f;
    ForceNetUpdate();
}

bool AMCCataclysmDebris::SweepContact(FVector Start, FVector& End, double Now)
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCCataclysmDebrisBody), false, this);
    Params.bFindInitialOverlaps = true;
    Params.AddIgnoredActor(Flood);
    Params.AddIgnoredActor(GetOwner());
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_Pawn);
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    TArray<FHitResult> Hits;
    GetWorld()->SweepMultiByObjectType(Hits, Start, End, GetActorQuat(), Objects, FCollisionShape::MakeBox(HalfExtent), Params);
    Hits.Sort([](const FHitResult& A, const FHitResult& B) { return A.Time < B.Time; });
    for (const FHitResult& Hit : Hits)
    {
        AActor* Other = Hit.GetActor();
        if (!IsValid(Other) || Other->IsA<AMCCataclysmDebris>()) continue;
        if (AMCToothCharacter* Hero = Cast<AMCToothCharacter>(Other))
        {
            if (IsWarning() || Hit.GetComponent() != Hero->GetCapsuleComponent()
                || !Hero->Status || !Hero->Status->IsAlive() || Hero->IsMimicCaptured() || Hero->SwallowedBy
                || !Hero->ToothPhysics || Hero->ToothPhysics->GetBodyState() != EMCBodyState::Standing) continue;
            const FVector Center = Hero->GetActorLocation();
            const float CapsuleHalf = Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            const FVector Foot = Center - FVector(0, 0, CapsuleHalf);
            const FQuat Rotation = GetActorQuat();
            const float BoxVerticalExtent = FMath::Abs(Rotation.GetAxisX().Z) * HalfExtent.X
                + FMath::Abs(Rotation.GetAxisY().Z) * HalfExtent.Y + FMath::Abs(Rotation.GetAxisZ().Z) * HalfExtent.Z;
            const UPrimitiveComponent* Base = Cast<UPrimitiveComponent>(Hero->GetMovementBaseObject());
            const AMCCataclysmRaft* RidingRaft = Base ? Cast<AMCCataclysmRaft>(Base->GetOwner()) : nullptr;
            const bool ActualRider = RidingRaft && RidingRaft->IsRider(Hero);
            const float WaterHeight = Flood->SurfaceHeightAt(Foot);
            FVector WaterProbe = Foot;
            if (ActualRider) WaterProbe.Z = FMath::Min(Foot.Z, double(WaterHeight - 1.f));
            // The swept visible box is the hit test. These extra height/water and
            // sight gates prevent contact through other floors or dry shelter.
            if (FMath::Abs(Center.Z - Hit.Location.Z) > CapsuleHalf + BoxVerticalExtent + 1.f
                || (!ActualRider && Foot.Z > WaterHeight + 1.f) || !Flood->Contains(WaterProbe)
                || !ClearContact(Hit, Start) || !ClaimPlayerContact(Hero, Now)) continue;
            const AMCCataclysmRaft* Rail = Hero->Grip ? Cast<AMCCataclysmRaft>(Hero->Grip->BraceTarget()) : nullptr;
            if (Rail && Rail->IsHoldingRail(Hero)) ++BracedContactCount;
            else
            {
                // Existing authoritative ragdoll/feedback path, with zero HP damage.
                FVector Direction = DriftVelocity.GetSafeNormal2D();
                if (Direction.IsNearlyZero()) Direction = Flood->StormStreamDirection().GetSafeNormal2D();
                Hero->ToothPhysics->ApplyHit(Direction * 380.f + FVector(0, 0, 110.f), Hit.ImpactPoint);
                ++PlayerImpactCount;
            }
            End = FMath::Lerp(Start, End, FMath::Clamp(Hit.Time, 0.f, 1.f));
            MarkImpact(Now, Hit.ImpactPoint);
            return true; // One plank/contact response; never also shock its rider's raft.
        }
        if (AMCCataclysmRaft* Raft = Cast<AMCCataclysmRaft>(Other))
        {
            if (IsWarning() || Hit.GetComponent() != Raft->GetDeckComponent() || !ClearContact(Hit, Start)) continue;
            Raft->ApplyDebrisImpulse(DriftVelocity.GetSafeNormal2D() * 120.f, Hit.ImpactPoint);
            ++RaftImpactCount;
            End = FMath::Lerp(Start, End, FMath::Clamp(Hit.Time, 0.f, 1.f));
            MarkImpact(Now, Hit.ImpactPoint);
            return true;
        }
        UPrimitiveComponent* Component = Hit.GetComponent();
        if (Component && Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
        {
            // Shoreline and solid scenery stop the body; it cannot pass through
            // a wall and hit a pawn found farther along the same sweep.
            StopDebris();
            return true;
        }
    }
    return false;
}

void AMCCataclysmDebris::Advance(float DeltaSeconds, double Now)
{
    if (!IsValid(Flood) || !Flood->IsActive() || !Flood->IsCataclysmStorm()
        || Flood->GetPhase() != EMCCoffeePhase::Holding || Now - SpawnedAt >= FMath::Clamp(LifetimeSeconds, 1.f, 12.f)
        || (RetireAt >= 0. && Now >= RetireAt)) { StopDebris(); return; }
    const float Dt = FMath::Clamp(DeltaSeconds, 0.f, .1f);
    const FVector Start = GetActorLocation();
    FVector Next = Start;
    if (RetireAt < 0.)
    {
        const FVector Current = Flood->FlowAtPosition(Start, this);
        const FVector Direction = Current.GetSafeNormal2D(UE_SMALL_NUMBER,Flood->StormStreamDirection());
        const float Speed = FMath::Clamp(280.f + SpeedOffset + .22f * float(FVector::DotProduct(Current, Direction)), 260.f, 440.f);
        const FVector Across = (Current - Direction * FVector::DotProduct(Current, Direction)).GetClampedToMaxSize(35.f);
        DriftVelocity = Direction * Speed + Across;
        Next += DriftVelocity * Dt;
        Next.Z = Flood->SurfaceHeightAt(Next) + HalfExtent.Z * .4f;
        if (!Flood->Contains(Next - FVector(0, 0, HalfExtent.Z * .4f))) { StopDebris(); return; }
        SweepContact(Start, Next, Now);
        if (IsActorBeingDestroyed()) return;
    }
    else
    {
        Next += DriftVelocity * Dt;
        Next.Z = Flood->SurfaceHeightAt(Next) + HalfExtent.Z * .4f - float(Now - LastImpactAt) * 260.f;
    }
    const float Age = float(Now - SpawnedAt);
    const float Yaw = Flood->StormStreamDirection().Rotation().Yaw + (Seed % 17 - 8) + SpinDegrees * Age;
    const FRotator Rotation(4.f * FMath::Sin(Age * 1.8f + Seed), Yaw, 5.f * FMath::Cos(Age * 1.4f + Seed));
    SetActorLocationAndRotation(Next, Rotation, false);
}

void AMCCataclysmDebris::UpdateCue(double Now)
{
    if (!bCueInitialized || !Cue->GetMaterial(0)) return;
    const float Pulse = .5f + .5f * FMath::Sin(float(Now - SpawnedAt) * 14.f);
    const bool Impact = LastImpactAt >= 0. && Now - LastImpactAt < ContactRetireSeconds;
    const FLinearColor Color = Impact ? FLinearColor(1.8f, 1.6f, 1.1f, .95f)
        : IsWarning() ? FLinearColor(1.5f, .42f, .015f, .35f + .6f * Pulse)
        : FLinearColor(1.1f, .30f, .008f, .70f);
    const TArray<FVector> Vertices = {{-48,-8,50.6f},{48,-8,50.6f},{48,8,50.6f},{-48,8,50.6f},
        {50.6f,-48,-8},{50.6f,48,-8},{50.6f,48,8},{50.6f,-48,8}};
    const TArray<int32> Indices = {0,1,2,0,2,3,4,5,6,4,6,7};
    TArray<FVector> Normals;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    for (int32 Index = 0; Index < Vertices.Num(); ++Index)
    {
        Normals.Add(Index < 4 ? FVector::UpVector : FVector::ForwardVector);
        UV.Add(FVector2D::ZeroVector); Colors.Add(Color); Tangents.Add(FProcMeshTangent(0, 1, 0));
    }
    if (Cue->GetNumSections() == 0) Cue->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, Colors, Tangents, false);
    else Cue->UpdateMeshSection_LinearColor(0, Vertices, Normals, UV, Colors, Tangents, false);
}

void AMCCataclysmDebris::OnRep_ReplicatedMovement()
{
    const FVector Previous = GetActorLocation();
    const FQuat PreviousRotation = GetActorQuat();
    Super::OnRep_ReplicatedMovement();
    ClientTarget = GetActorLocation();
    ClientRotation = GetActorQuat();
    ClientSnapshotAt = Clock();
    if (bClientPoseInitialized && FVector::DistSquared(Previous, ClientTarget) < FMath::Square(500.f))
        SetActorLocationAndRotation(Previous, PreviousRotation, false);
    bClientPoseInitialized = true;
}

void AMCCataclysmDebris::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (SpawnedAt < 0.) return;
    const double Now = Clock();
    if (HasAuthority()) Advance(DeltaSeconds, Now);
    else if (bClientPoseInitialized)
    {
        const FVector Target = ClientTarget + DriftVelocity * FMath::Clamp(float(Now - ClientSnapshotAt), 0.f, .075f);
        SetActorLocationAndRotation(FMath::VInterpTo(GetActorLocation(), Target, DeltaSeconds, 20.f),
            FQuat::Slerp(GetActorQuat(), ClientRotation, FMath::Clamp(DeltaSeconds * 20.f, 0.f, 1.f)), false);
    }
    if (!IsActorBeingDestroyed()) UpdateCue(Now);
}

void AMCCataclysmDebris::EndPlay(const EEndPlayReason::Type Reason)
{
    if (TickFlood.IsValid()) RemoveTickPrerequisiteActor(TickFlood.Get());
    if (Reason != EEndPlayReason::Destroyed)
    {
        const UWorld* World = GetWorld();
        RecentContacts.RemoveAll([World](const FRecentDebrisContact& Entry)
        {
            return !Entry.Player.IsValid() || Entry.Player->GetWorld() == World;
        });
    }
    Super::EndPlay(Reason);
}

void AMCCataclysmDebris::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCCataclysmDebris, Flood);
    DOREPLIFETIME(AMCCataclysmDebris, Tongue);
    DOREPLIFETIME(AMCCataclysmDebris, Seed);
    DOREPLIFETIME(AMCCataclysmDebris, SpawnedAt);
    DOREPLIFETIME(AMCCataclysmDebris, DriftVelocity);
    DOREPLIFETIME(AMCCataclysmDebris, ImpactCount);
    DOREPLIFETIME(AMCCataclysmDebris, PlayerImpactCount);
    DOREPLIFETIME(AMCCataclysmDebris, BracedContactCount);
    DOREPLIFETIME(AMCCataclysmDebris, RaftImpactCount);
    DOREPLIFETIME(AMCCataclysmDebris, LastImpactAt);
    DOREPLIFETIME(AMCCataclysmDebris, RetireAt);
}
