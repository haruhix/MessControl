#include "MCCataclysmRaft.h"

#include "MCCoffeeFlood.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "MCGripComponent.h"
#include "MCInventoryComponent.h"
#include "MCRowingStroke.h"
#include "MCTongue.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"

namespace
{
    constexpr float FloatDraft = 21.f;
    constexpr float SinkDepth = 112.f;
    constexpr float RepeatGrace = 1.25f;
    constexpr int32 RingSegments = 48;
    constexpr float CurrentScale = .22f;
    constexpr double RowInputLifetime = .3;

    float BoundedRowingSetting(float Value, float Default, float Min, float Max)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value,Min,Max) : Default;
    }

    void HullEnvelope(const UStaticMeshComponent* Deck, FVector& Origin, FVector& Extent)
    {
        const FBoxSphereBounds Bounds = Deck->GetStaticMesh()->GetBounds();
        const FVector Scale = Deck->GetComponentScale().GetAbs();
        Origin = Bounds.Origin * Scale;
        Extent = (Bounds.BoxExtent * Scale - FVector(1.f)).ComponentMax(FVector(2.f));
        // The imported visual ropes hang six cm below the UCX log bottoms.
        // Preserve the original 21 cm beached draft; those loose rope ends must
        // not make the floor block every pose while the water drains.
        const float Bottom = FMath::Max(float((Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale.Z),-FloatDraft);
        const float Top = float((Bounds.Origin.Z+Bounds.BoxExtent.Z)*Scale.Z);
        Origin.Z = (Bottom+Top)*.5f;
        Extent.Z = FMath::Max(2.f,(Top-Bottom)*.5f-1.f);
    }

    void Ring(UProceduralMeshComponent* Mesh, float RadiusX, float RadiusY, float Width,
        float Height, float Phase, FLinearColor Color, bool bCreate)
    {
        TArray<FVector> Vertices, Normals;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
        TArray<int32> Triangles;
        Vertices.Reserve((RingSegments + 1) * 3);
        for (int32 I = 0; I <= RingSegments; ++I)
        {
            const float T = float(I) / RingSegments;
            const float Angle = T * UE_TWO_PI;
            const float Ripple = 1.f + .015f * FMath::Sin(Angle * 5.f + Phase);
            for (int32 Column = 0; Column < 3; ++Column)
            {
                const float Offset = (Column - 1) * Width;
                Vertices.Add(FVector((RadiusX * Ripple + Offset) * FMath::Cos(Angle),
                    (RadiusY * Ripple + Offset) * FMath::Sin(Angle), Height));
                Normals.Add(FVector::UpVector);
                UVs.Add(FVector2D(T, Column * .5f));
                FLinearColor VertexColor = Color;
                VertexColor.A *= Column == 1 ? 1.f : 0.f;
                Colors.Add(VertexColor);
                Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
            }
            if (I < RingSegments)
            {
                for (int32 Column = 0; Column < 2; ++Column)
                {
                    const int32 A = I * 3 + Column, B = A + 3;
                    Triangles.Append({A, B + 1, B, A, A + 1, B + 1});
                }
            }
        }
        if (bCreate)
            Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
        else
            Mesh->UpdateMeshSection_LinearColor(0, Vertices, Normals, UVs, Colors, Tangents, false);
    }
}

AMCCataclysmRaft::AMCCataclysmRaft()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    // Input, collisions and debris choose one server pose. Peers never integrate their own drift.
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30.f);
    SetMinNetUpdateFrequency(15.f);
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    Deck = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RaftDeck"));
    SetRootComponent(Deck);
    Deck->SetMobility(EComponentMobility::Movable);
    Deck->SetSimulatePhysics(false);
    Deck->SetCollisionObjectType(ECC_WorldDynamic);
    Deck->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Deck->SetCollisionResponseToAllChannels(ECR_Block);
    Deck->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    Deck->CanCharacterStepUpOn = ECB_Yes;
    Deck->SetCanEverAffectNavigation(false);
    Deck->SetGenerateOverlapEvents(false);
    WarningOutline = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RaftLoadWarning"));
    WaterWake = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RaftWaterWake"));
    LeftOar = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RaftLeftRowingBrush"));
    RightOar = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RaftRightRowingBrush"));
    for (UProceduralMeshComponent* Cue : {WarningOutline.Get(), WaterWake.Get()})
    {
        Cue->SetupAttachment(Deck);
        Cue->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Cue->SetCastShadow(false);
        Cue->SetCanEverAffectNavigation(false);
        Cue->SetVisibility(false);
        Cue->SetTranslucentSortPriority(2);
    }
    WaterWake->SetAbsolute(true, true, true);
    for (UStaticMeshComponent* Oar : {LeftOar.Get(), RightOar.Get()})
    {
        Oar->SetupAttachment(Deck);
        Oar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Oar->SetCanEverAffectNavigation(false);
        Oar->SetVisibility(false);
    }
    RowingPlayers.Init(nullptr, Capacity);
    SeatPlayers.Init(nullptr, Capacity);
    RowingDirections.Init(FVector2D::ZeroVector, Capacity);
    RowingStartedAt.Init(0., Capacity);
}

void AMCCataclysmRaft::BeginPlay()
{
    Super::BeginPlay();
    if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/Cataclysm/Raft/SM_CataclysmRaft.SM_CataclysmRaft")))
        Deck->SetStaticMesh(Mesh);
    else
        UE_LOG(LogTemp, Error, TEXT("Cataclysm raft mesh missing; run import_cataclysm_raft.py before playing."));
    InitializeCues();
    OnRep_Flood();
    UpdatePose(0.f, Clock());
}

void AMCCataclysmRaft::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority())
        for (int32 Slot=0; Slot<SeatPlayers.Num(); ++Slot)
            if (AMCToothCharacter* Hero=SeatPlayers[Slot].Get()) ReleaseSeat(Hero);
    Super::EndPlay(Reason);
}

void AMCCataclysmRaft::SetupForFlood(AMCCoffeeFlood* InFlood, FVector GroundAnchor, float SurfaceOffset)
{
    if (!HasAuthority() || GroundAnchor.ContainsNaN() || !FMath::IsFinite(SurfaceOffset)) return;
    Flood = InFlood;
    OnRep_Flood();
    Anchor = GroundAnchor;
    LastSupportPoint = GroundAnchor;
    bHasSupportPoint = true;
    NextSupportScanAt = 0.;
    HorizontalVelocity = FVector::ZeroVector;
    YawVelocity = ImpactPitch = ImpactRoll = 0.f;
    RowingRequests.Reset();
    UpdateRowing(Clock());
    if (!SupportTongue.IsValid())
        for (TActorIterator<AMCTongue> It(GetWorld()); It; ++It)
            if (!It->CurrentVertices().IsEmpty()) { SupportTongue = *It; break; }
    WaterlineOffset = FMath::Clamp(SurfaceOffset, -10.f, 30.f);
    if (!bConfigured)
    {
        AnchorYaw = GetActorRotation().Yaw;
        FloatStartedAt = Clock();
    }
    bConfigured = true;
    SurfaceWorldZ = FMath::Max(float(Anchor.Z) + FloatDraft,
        Flood ? Flood->SurfaceHeightAt(Anchor) + WaterlineOffset : float(Anchor.Z) + FloatDraft);
    UpdatePose(0.f, Clock());
    ForceNetUpdate();
}

void AMCCataclysmRaft::PostNetReceiveLocationAndRotation()
{
    Super::PostNetReceiveLocationAndRotation();
    // Jumping off this kinematic base must inherit the same velocity on the owning client.
    if (Deck) Deck->ComponentVelocity = GetReplicatedMovement().LinearVelocity;
}

void AMCCataclysmRaft::UpdateSurface(float WaterWorldZ)
{
    if (HasAuthority() && FMath::IsFinite(WaterWorldZ))
        SurfaceWorldZ = FMath::Max(float(Anchor.Z) + FloatDraft, WaterWorldZ + WaterlineOffset);
}

void AMCCataclysmRaft::OnRep_Flood()
{
    if (TickFlood.IsValid()) RemoveTickPrerequisiteActor(TickFlood.Get());
    TickFlood = Flood;
    if (Flood) AddTickPrerequisiteActor(Flood);
}

double AMCCataclysmRaft::Clock() const
{
    const UWorld* World = GetWorld();
    if (!World) return 0.;
    if (const AGameStateBase* State = World->GetGameState()) return State->GetServerWorldTimeSeconds();
    return World->GetTimeSeconds();
}

bool AMCCataclysmRaft::IsSubmerging() const
{
    const double Age = Clock() - SinkStartedAt;
    return SinkStartedAt >= 0. && Age >= 0. && Age < SubmergeSeconds;
}

bool AMCCataclysmRaft::IsOverloaded() const
{
    return OccupantCount > Capacity || WarningStartedAt >= 0. || IsSubmerging();
}

bool AMCCataclysmRaft::IsBoardable() const
{
    return bConfigured && Flood && Flood->IsActive() && Deck->GetStaticMesh() && !IsSubmerging()
        && SeatPlayers.ContainsByPredicate([](const auto& Player) { return !IsValid(Player.Get()); });
}

UPrimitiveComponent* AMCCataclysmRaft::GetDeckComponent() const { return Deck; }

bool AMCCataclysmRaft::IsRider(const AMCToothCharacter* Hero) const
{
    if (!IsValid(Hero) || Hero->GetWorld() != GetWorld()) return false;
    return SeatIndex(Hero)!=INDEX_NONE && Hero->SeatedRaft==this;
}

int32 AMCCataclysmRaft::SeatIndex(const AMCToothCharacter* Hero) const
{
    return Hero ? SeatPlayers.IndexOfByPredicate([Hero](const auto& Player) { return Player.Get()==Hero; }) : INDEX_NONE;
}

FVector AMCCataclysmRaft::SeatFootPoint(int32 Slot) const
{
    return Deck->GetComponentTransform().TransformPosition(FVector(0,Slot==0?-46.f:46.f,DeckTop));
}

bool AMCCataclysmRaft::TryClaimSeat(AMCToothCharacter* Hero)
{
    if (!HasAuthority() || !IsValid(Hero) || Hero->GetWorld()!=GetWorld() || !Hero->CanWork()
        || !Hero->GetController() || !Hero->GetPlayerState() || Hero->GetPlayerState()->IsSpectator()
        || Hero->GetPlayerState()->IsOnlyASpectator() || !bConfigured || !Flood || !Flood->IsActive() || IsSubmerging()
        || (Hero->IsSeatedOnRaft() && Hero->SeatedRaft!=this)) return false;
    if (SeatIndex(Hero)!=INDEX_NONE) return true;
    // Also handles simultaneous boarding attempts: only authority reserves slots.
    if (!IsBoardable()) return false;
    const int32 Slot=SeatPlayers.IndexOfByPredicate([](const auto& Player) { return !IsValid(Player.Get()); });
    const FVector LocalFoot=Deck->GetComponentTransform().InverseTransformPosition(Hero->GetActorLocation()
        -FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    if (Slot==INDEX_NONE || FMath::Abs(LocalFoot.X)>120.f || FMath::Abs(LocalFoot.Y)>85.f
        || FMath::Abs(LocalFoot.Z-DeckTop)>15.f) return false;
    SeatPlayers[Slot]=Hero;
    OccupantCount=0;
    for (const auto& Player:SeatPlayers) if (IsValid(Player.Get())) ++OccupantCount;
    Hero->SetRaftSeat(this,Slot);
    ForceNetUpdate();
    return true;
}

void AMCCataclysmRaft::ReleaseSeat(AMCToothCharacter* Hero,FVector LaunchVelocity)
{
    if (!HasAuthority() || !Hero) return;
    const int32 Slot=SeatIndex(Hero);
    if (Slot!=INDEX_NONE) SeatPlayers[Slot]=nullptr;
    SetRowingInput(Hero,FVector2D::ZeroVector,false);
    if (Hero->SeatedRaft==this) Hero->SetRaftSeat(nullptr,INDEX_NONE);
    OccupantCount=0;
    for (const auto& Player:SeatPlayers) if (IsValid(Player.Get())) ++OccupantCount;
    if (!LaunchVelocity.ContainsNaN() && !LaunchVelocity.IsNearlyZero())
        Hero->LaunchCharacter(LaunchVelocity,true,true);
    ForceNetUpdate();
}

void AMCCataclysmRaft::ValidateSeats()
{
    for (int32 Slot=0; Slot<SeatPlayers.Num(); ++Slot)
    {
        AMCToothCharacter* Hero=SeatPlayers[Slot].Get();
        if (!IsValid(Hero)) { SeatPlayers[Slot]=nullptr; continue; }
        if (!Flood || !Flood->IsActive() || !Hero->GetController() || !Hero->CanWork()
            || Hero->HasFrozenLegs() || Hero->SeatedRaft!=this || Hero->RaftSeatIndex!=Slot
            || !Hero->GetPlayerState() || Hero->GetPlayerState()->IsSpectator()
            || Hero->GetPlayerState()->IsOnlyASpectator()) ReleaseSeat(Hero);
    }
    // Jumping onto the deck uses the same slots as the hand-over-rail mantle.
    // A third passenger cannot bypass capacity by landing on its collision.
    for (TActorIterator<AMCToothCharacter> It(GetWorld()); It; ++It)
    {
        AMCToothCharacter* Hero=*It;
        if (Hero->IsSeatedOnRaft() || Hero->GetMovementBaseObject()!=Deck || !Hero->CanWork()) continue;
        if (!TryClaimSeat(Hero))
        {
            FVector Out=(Hero->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
            if (Out.IsNearlyZero()) Out=GetActorRightVector();
            Hero->LaunchCharacter(Out*220.f+FVector(0,0,160.f),true,true);
        }
    }
}

bool AMCCataclysmRaft::IsHoldingRail(const AMCToothCharacter* Hero) const
{
    return IsRider(Hero) && Hero->CanWork() && Hero->Grip && Hero->Grip->IsBracing()
        && Hero->Grip->BraceTarget() == this && Hero->Grip->Brace.Component == Deck;
}

bool AMCCataclysmRaft::SetRowingInput(AMCToothCharacter* Hero, FVector2D Direction, bool Held)
{
    if (!HasAuthority() || !IsValid(Hero) || Hero->GetWorld() != GetWorld()) return false;
    const int32 Existing = RowingRequests.IndexOfByPredicate([&](const FRowingRequest& Row) { return Row.Hero.Get() == Hero; });
    if (!Held)
    {
        if (Existing != INDEX_NONE) RowingRequests.RemoveAt(Existing);
        return true;
    }
    if (!FMath::IsFinite(Direction.X) || !FMath::IsFinite(Direction.Y) || !Hero->CanWork()
        || !IsRider(Hero) || !Hero->GetPlayerState() || Hero->GetPlayerState()->IsSpectator()
        || !Flood || !Flood->IsActive() || !Flood->IsCataclysmStorm() || IsSubmerging())
    {
        if (Existing != INDEX_NONE) RowingRequests.RemoveAt(Existing);
        return false;
    }
    Direction = Direction.GetClampedToMaxSize(1.f);
    if (Direction.IsNearlyZero()) { if (Existing!=INDEX_NONE) RowingRequests.RemoveAt(Existing); return true; }
    int32 Index = Existing;
    if (Index == INDEX_NONE)
    {
        if (RowingRequests.Num() >= Capacity) return false;
        const int32 Oar = SeatIndex(Hero);
        if (Oar<0 || Oar>=Capacity) return false;
        Index = RowingRequests.AddDefaulted();
        RowingRequests[Index].Hero = Hero;
        RowingRequests[Index].Oar = Oar;
        // Shared phase makes equal inputs add and opposed inputs cancel exactly.
        RowingRequests[Index].StartedAt = FloatStartedAt;
    }
    RowingRequests[Index].Direction = Direction;
    RowingRequests[Index].RefreshedAt = Clock();
    return true;
}

void AMCCataclysmRaft::UpdateRowing(double Now)
{
    if (!HasAuthority()) return;
    TArray<AMCToothCharacter*> Riders;
    CollectRiders(Riders);
    RowingRequests.RemoveAll([&](const FRowingRequest& Row)
    {
        const AMCToothCharacter* Hero = Row.Hero.Get();
        return !Hero || Now-Row.RefreshedAt > RowInputLifetime || !Riders.Contains(Hero) || !Hero->CanWork()
            || !Flood || !Flood->IsActive()
            || !Flood->IsCataclysmStorm() || IsSubmerging();
    });
    const int32 Before = RowingCount;
    RowingCount = RowingRequests.Num();
    RowingDirection = FVector2D::ZeroVector;
    RowingPlayers.Init(nullptr, Capacity);
    RowingDirections.Init(FVector2D::ZeroVector, Capacity);
    RowingStartedAt.Init(0., Capacity);
    for (const FRowingRequest& Row : RowingRequests)
    {
        RowingPlayers[Row.Oar] = Row.Hero.Get();
        RowingDirections[Row.Oar] = Row.Direction;
        RowingStartedAt[Row.Oar] = Row.StartedAt;
        RowingDirection += Row.Direction;
    }
    if (Before != RowingCount) ForceNetUpdate();
}

bool AMCCataclysmRaft::GetRowingHandTargets(const AMCToothCharacter* Hero, FVector& Left, FVector& Right) const
{
    if(!Hero || !Hero->Brush || !Hero->BrushPivot) return false;
    const UStaticMeshComponent* Oar = RowingBrushFor(Hero);
    if (!Oar || !Oar->IsVisible() || !Oar->GetStaticMesh()) return false;
    const FTransform Pose = Oar->GetComponentTransform();
    const FTransform Mount=Hero->Brush->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform();
    Right=(Mount.Inverse()*Pose).GetLocation();
    // Fixed hand order while changing sides avoids overlapping or exchanging
    // palms halfway through walking across the deck.
    Left=Right+Pose.GetUnitAxis(EAxis::X)*20.f;
    return true;
}

float AMCCataclysmRaft::RowingStrokePhase(const AMCToothCharacter* Hero) const
{
    const int32 Side = Hero ? RowingPlayers.IndexOfByPredicate([&](const TObjectPtr<AMCToothCharacter>& Player) { return Player.Get() == Hero; }) : 0;
    const double Started = RowingStartedAt.IsValidIndex(Side) ? RowingStartedAt[Side] : FloatStartedAt;
    const double Age = FMath::Max(0.,Clock()-Started);
    return float(FMath::Fmod(Age,MCRowingStroke::CycleSeconds)*UE_TWO_PI/MCRowingStroke::CycleSeconds);
}

FVector AMCCataclysmRaft::RowingDirectionFor(const AMCToothCharacter* Hero) const
{
    const int32 Slot=RowingPlayers.IndexOfByPredicate([Hero](const TObjectPtr<AMCToothCharacter>& Player){return Player.Get()==Hero;});
    return RowingDirections.IsValidIndex(Slot) ? FRotator(0,GetActorRotation().Yaw,0).RotateVector(FVector(RowingDirections[Slot].X,RowingDirections[Slot].Y,0)) : FVector::ZeroVector;
}

const UStaticMeshComponent* AMCCataclysmRaft::RowingBrushFor(const AMCToothCharacter* Hero) const
{
    if(!Hero) return nullptr;
    const int32 Slot=RowingPlayers.IndexOfByPredicate([Hero](const TObjectPtr<AMCToothCharacter>& Player){return Player.Get()==Hero;});
    return Slot==0?LeftOar.Get():Slot==1?RightOar.Get():nullptr;
}

FVector AMCCataclysmRaft::RowingVisualDirectionFor(const AMCToothCharacter* Hero) const
{
    const int32 Slot=RowingPlayers.IndexOfByPredicate([Hero](const TObjectPtr<AMCToothCharacter>& Player){return Player.Get()==Hero;});
    return Slot>=0 && Slot<Capacity && VisualRowers[Slot].Get()==Hero
        ? VisualRowingDirection[Slot] : RowingDirectionFor(Hero);
}

void AMCCataclysmRaft::AlignRowingBrushToHand(const AMCToothCharacter* Hero,const FTransform& HandWorld)
{
    if(!Hero || !Hero->Brush || !Hero->BrushPivot || HandWorld.ContainsNaN()) return;
    const int32 Slot=RowingPlayers.IndexOfByPredicate([Hero](const TObjectPtr<AMCToothCharacter>& Player){return Player.Get()==Hero;});
    if(Slot<0 || Slot>=Capacity) return;
    auto* Oar=Slot==0?LeftOar.Get():RightOar.Get();
    if(!Oar || !Oar->IsVisible()) return;
    // Follow the actual blended wrist during entry. On release the inventory's
    // original brush uses this same mount and wrist, so presentation can switch
    // back without a floating shaft or a discontinuity at the grip.
    const FTransform Mount=Hero->Brush->GetRelativeTransform()*Hero->BrushPivot->GetRelativeTransform();
    const FTransform ToolWorld=Mount*HandWorld;
    Oar->SetWorldLocationAndRotation(ToolWorld.GetLocation(),ToolWorld.GetRotation());
}

float AMCCataclysmRaft::RowingSideFor(const AMCToothCharacter* Hero) const
{
    if(!Hero) return 0.f;
    const int32 Slot=RowingPlayers.IndexOfByPredicate([Hero](const TObjectPtr<AMCToothCharacter>& Player){return Player.Get()==Hero;});
    return Slot>=0 && Slot<Capacity ? VisualPaddleSide[Slot] : 0.f;
}

void AMCCataclysmRaft::ApplyDebrisImpulse(FVector WorldVelocityImpulse, FVector ContactPoint)
{
    if (!HasAuthority() || !bConfigured || !Flood || !Flood->IsActive() || !Flood->IsCataclysmStorm()
        || WorldVelocityImpulse.ContainsNaN() || ContactPoint.ContainsNaN()) return;
    WorldVelocityImpulse.Z = 0;
    const FVector Kick = WorldVelocityImpulse.GetClampedToMaxSize(140.f);
    const FVector Before = HorizontalVelocity;
    const float SpeedLimit = BoundedRowingSetting(MaxTravelSpeed,480.f,60.f,800.f);
    HorizontalVelocity = (HorizontalVelocity + Kick).GetClampedToMaxSize(SpeedLimit);
    const FVector Applied = HorizontalVelocity-Before;
    const FVector Arm = (ContactPoint-GetActorLocation()).GetClampedToMaxSize(200.f);
    const float YawKick = FMath::Clamp(float(FVector::CrossProduct(Arm,Applied).Z)/550.f,-24.f,24.f);
    const float YawLimit = BoundedRowingSetting(MaxYawSpeed,65.f,1.f,180.f);
    YawVelocity = FMath::Clamp(YawVelocity+YawKick,-YawLimit,YawLimit);
    ImpactPitch = FMath::Clamp(ImpactPitch + float(FVector::DotProduct(Applied, GetActorForwardVector()))*.015f, -3.f, 3.f);
    ImpactRoll = FMath::Clamp(ImpactRoll - float(FVector::DotProduct(Applied, GetActorRightVector()))*.018f, -3.f, 3.f);
    // A collision ejects the closest occupied side, rather than adding an impulse
    // that a fixed-seat transform would silently erase on the following tick.
    if (Kick.Size()>=BoundedRowingSetting(DebrisEjectThreshold,85.f,20.f,140.f))
    {
        AMCToothCharacter* Closest=nullptr; float Best=MAX_flt;
        for (const auto& Player:SeatPlayers)
            if (AMCToothCharacter* Hero=Player.Get(); IsValid(Hero))
            {
                const float Distance=FVector::DistSquared2D(ContactPoint,Hero->GetActorLocation());
                if (Distance<Best) {Closest=Hero; Best=Distance;}
            }
        if (Closest)
        {
            // Clear the occupied side of the hull. A purely world-directed kick
            // can cross the other passenger and land back in the automatic seat.
            const FVector Out = GetActorRightVector()*(SeatIndex(Closest)==0?-1.f:1.f);
            FVector Launch = HorizontalVelocity+Kick*2.6f;
            Launch += Out*FMath::Max(0.f,280.f-float(FVector::DotProduct(Launch,Out)));
            ReleaseSeat(Closest,Launch+FVector(0,0,210.f));
        }
    }
    ForceNetUpdate();
}

FCollisionQueryParams AMCCataclysmRaft::TravelQuery() const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCCataclysmRaftTravel), false, this);
    if (Flood) Query.AddIgnoredActor(Flood);
    // Riders are moved by CMC's base logic. Swimmers can approach the moving hull;
    // their capsule must not pin the much larger raft in place.
    for (TActorIterator<AMCToothCharacter> It(GetWorld()); It; ++It) Query.AddIgnoredActor(*It);
    return Query;
}

bool AMCCataclysmRaft::ClearHullPose(FVector Location, FRotator Rotation, const FCollisionQueryParams& Query) const
{
    if (!Deck->GetStaticMesh()) return false;
    FVector Origin,Extent;
    HullEnvelope(Deck,Origin,Extent);
    const FVector Center = Location + Rotation.Quaternion().RotateVector(Origin);
    return !GetWorld()->OverlapBlockingTestByChannel(Center, Rotation.Quaternion(), Deck->GetCollisionObjectType(),
        FCollisionShape::MakeBox(Extent), Query, FCollisionResponseParams(Deck->GetCollisionResponseToChannels()));
}

bool AMCCataclysmRaft::SupportedFloor(FVector Position, float Yaw, FVector& FloorPoint) const
{
    const AMCTongue* Tongue = SupportTongue.Get();
    if (!Tongue || !Deck->GetStaticMesh()) return false;
    const FBoxSphereBounds Bounds = Deck->GetStaticMesh()->GetBounds();
    const FVector Scale = Deck->GetComponentScale().GetAbs();
    const FVector Half = Bounds.BoxExtent * Scale;
    const FVector Origin = Bounds.Origin * Scale;
    const float Radius = FVector2D(Half.X, Half.Y).Size()+FVector2D(Origin.X,Origin.Y).Size()+20.f;
    FHitResult Floor;
    if (!Tongue->InteriorSurfacePoint(Position, Radius, Floor)) return false;
    FloorPoint = Floor.ImpactPoint;
    const FQuat Heading = FRotator(0,Yaw,0).Quaternion();
    for (float X : {-float(Half.X), float(Half.X)}) for (float Y : {-float(Half.Y), float(Half.Y)})
    {
        FHitResult Corner;
        if (!Tongue->SurfacePoint(Position+Heading.RotateVector(FVector(X+Origin.X,Y+Origin.Y,0)), Corner)) return false;
        FloorPoint.Z = FMath::Max(FloorPoint.Z, Corner.ImpactPoint.Z);
    }
    return true;
}

void AMCCataclysmRaft::UpdateDrift(float DeltaSeconds, double Now)
{
    if (!HasAuthority() || DeltaSeconds <= 0.f || !bConfigured || !Deck->GetStaticMesh()) return;
    const float Dt = FMath::Min(DeltaSeconds, .25f);
    ImpactPitch *= FMath::Exp(-Dt*3.f);
    ImpactRoll *= FMath::Exp(-Dt*3.f);
    const float Water = Flood && Flood->IsActive() ? Flood->SurfaceHeightAt(GetActorLocation()) : SurfaceWorldZ;
    if (!Flood || !Flood->IsActive() || !Flood->IsCataclysmStorm()
        || IsSubmerging() || Water-Anchor.Z-FloatDraft < 30.f)
    {
        HorizontalVelocity *= FMath::Exp(-Dt*5.f);
        YawVelocity *= FMath::Exp(-Dt*4.f);
        return;
    }
    const FCollisionQueryParams Query = TravelQuery();
    const FCollisionResponseParams Response(Deck->GetCollisionResponseToChannels());
    FVector HullOrigin,Extent;
    HullEnvelope(Deck,HullOrigin,Extent);
    const float Radius = FVector2D(Extent.X,Extent.Y).Size()+10.f;
    const FVector Min = Flood->ArenaCenter-Flood->HalfSize+FVector(Radius,Radius,0);
    const FVector Max = Flood->ArenaCenter+Flood->HalfSize-FVector(Radius,Radius,0);
    if (Min.X >= Max.X || Min.Y >= Max.Y) { HorizontalVelocity = FVector::ZeroVector; return; }
    FVector Position(Anchor.X,Anchor.Y,GetActorLocation().Z);
    const float Thrust = BoundedRowingSetting(RowAcceleration,360.f,0.f,1000.f);
    const float SpeedLimit = BoundedRowingSetting(MaxTravelSpeed,480.f,60.f,800.f);
    const float LinearDamping = BoundedRowingSetting(TravelDrag,1.1f,.1f,5.f);
    const float Inertia = BoundedRowingSetting(YawInertiaRadiusSquared,18000.f,1000.f,100000.f);
    const float BladeOffset = BoundedRowingSetting(PaddleLeverOffset,35.f,0.f,100.f);
    const float YawDamping = BoundedRowingSetting(AngularDrag,1.6f,.1f,5.f);
    const float YawLimit = BoundedRowingSetting(MaxYawSpeed,65.f,1.f,180.f);
    const float TurnLimit = BoundedRowingSetting(MaxYawAcceleration,150.f,10.f,720.f);
    float Remaining = Dt;
    while (Remaining > UE_SMALL_NUMBER)
    {
        const float Step = FMath::Min(Remaining,.033f);
        Remaining -= Step;
        // Sample the loaded part of each stroke at this substep, rather than
        // stretching one render-frame sample over a whole hitch.
        const double StrokeTime = Now-Remaining-Step*.5f;
        FVector PaddleAcceleration = FVector::ZeroVector;
        float PaddleYawAcceleration = 0.f;
        const FQuat HullTurn = FRotator(0,AnchorYaw,0).Quaternion();
        for (const FRowingRequest& Row : RowingRequests)
            if (const AMCToothCharacter* Hero = Row.Hero.Get())
            {
                const double Age = FMath::Max(0.,StrokeTime-Row.StartedAt);
                const float Phase = float(FMath::Fmod(Age,MCRowingStroke::CycleSeconds)*UE_TWO_PI/MCRowingStroke::CycleSeconds);
                const auto Stroke = MCRowingStroke::Sample(Phase);
                const FVector Aim=HullTurn.RotateVector(FVector(Row.Direction.X,Row.Direction.Y,0));
                const FVector Force = Aim*Thrust*(2.1f*Stroke.Power);
                const FVector RowerArm = HullTurn.RotateVector(FVector(0,Row.Oar==0?-46.f:46.f,0));
                const FVector Arm = (RowerArm+HullTurn.GetRightVector()*(Row.Oar==0?-BladeOffset:BladeOffset)).GetClampedToMaxSize2D(155.f);
                // Preserve the sum of all forces and their actual lever arms.
                // Opposite strokes can cancel translation while adding rotation.
                PaddleAcceleration += Force;
                PaddleYawAcceleration += FMath::RadiansToDegrees(float(FVector::CrossProduct(Arm,Force).Z)/Inertia);
            }
        FVector EdgeForce = FVector::ZeroVector;
        // Turn the approach to a boundary into pressure the crew can row against,
        // rather than teleporting or reflecting a raft across the arena.
        EdgeForce.X = 85.f * (FMath::Clamp(float(150.-(Position.X-Min.X))/150.f,0.f,1.f)
            - FMath::Clamp(float(150.-(Max.X-Position.X))/150.f,0.f,1.f));
        EdgeForce.Y = 85.f * (FMath::Clamp(float(150.-(Position.Y-Min.Y))/150.f,0.f,1.f)
            - FMath::Clamp(float(150.-(Max.Y-Position.Y))/150.f,0.f,1.f));
        const FVector WaterAcceleration=Flood->FlowAtPosition(Position,this)*CurrentScale;
        FVector Acceleration = WaterAcceleration + PaddleAcceleration + EdgeForce;
        Acceleration.Z = 0;
        const float TravelDecay = FMath::Exp(-LinearDamping*Step);
        HorizontalVelocity = (HorizontalVelocity*TravelDecay+Acceleration*((1.f-TravelDecay)/LinearDamping)).GetClampedToMaxSize(SpeedLimit);
        FVector Candidate = Position+HorizontalVelocity*Step;
        // Clamp only the requested step; an unexpected bad spawn is never snapped inward.
        if (Position.X >= Min.X && Position.X <= Max.X) Candidate.X = FMath::Clamp(Candidate.X,Min.X,Max.X);
        if (Position.Y >= Min.Y && Position.Y <= Max.Y) Candidate.Y = FMath::Clamp(Candidate.Y,Min.Y,Max.Y);
        const FVector Travel = Candidate-Position;
        if (FMath::Abs(Travel.X) < FMath::Abs(HorizontalVelocity.X*Step)*.9f) HorizontalVelocity.X = 0;
        if (FMath::Abs(Travel.Y) < FMath::Abs(HorizontalVelocity.Y*Step)*.9f) HorizontalVelocity.Y = 0;
        FVector FloorPoint = LastSupportPoint;
        const bool CheckSupport = !bHasSupportPoint || Now >= NextSupportScanAt || FVector::DistSquared2D(Candidate,LastSupportPoint)>FMath::Square(18.f);
        if (CheckSupport && !SupportedFloor(Candidate,AnchorYaw,FloorPoint))
        {
            HorizontalVelocity *= -.15f;
            break;
        }
        const FQuat Rotation = GetActorQuat();
        const FVector Origin = Rotation.RotateVector(HullOrigin);
        FHitResult Hit;
        if (GetWorld()->SweepSingleByChannel(Hit,Position+Origin,Candidate+Origin,Rotation,
            Deck->GetCollisionObjectType(),FCollisionShape::MakeBox(Extent),Query,Response))
        {
            if (!Hit.bStartPenetrating) Position += Travel*FMath::Max(0.f,Hit.Time-.01f);
            const FVector Normal = Hit.Normal.GetSafeNormal2D();
            const float Into = float(FVector::DotProduct(HorizontalVelocity,Normal));
            if (Into < 0) HorizontalVelocity -= Normal*Into;
            HorizontalVelocity *= .65f;
            break;
        }
        Position = Candidate;
        if (CheckSupport)
        {
            LastSupportPoint = FloorPoint;
            bHasSupportPoint = true;
            NextSupportScanAt = Now+.12;
        }
        Anchor.X = Position.X; Anchor.Y = Position.Y;
        Anchor.Z = LastSupportPoint.Z;
        // Water pressure on the leading hull and an uneven passing crest can
        // turn the raft; rowers counter it with their own lever arms. Preserve
        // angular inertia rather than steering automatically toward an aim.
        const FVector Forward=FRotator(0,AnchorYaw,0).Vector();
        const FVector Across(-Forward.Y,Forward.X,0);
        const float CrestSlope=(Flood->StormWaveHeightAt(Position+Across*90.f)
            -Flood->StormWaveHeightAt(Position-Across*90.f))/180.f;
        const float WaterYawAcceleration=FMath::RadiansToDegrees(float(FVector::CrossProduct(Forward*30.f,WaterAcceleration).Z)/Inertia)
            + CrestSlope*40.f;
        const float TurnAcceleration=FMath::Clamp(PaddleYawAcceleration+WaterYawAcceleration,-TurnLimit,TurnLimit);
        const float YawDecay=FMath::Exp(-YawDamping*Step);
        YawVelocity=FMath::Clamp(YawVelocity*YawDecay+TurnAcceleration*((1.f-YawDecay)/YawDamping),-YawLimit,YawLimit);
        const float NextYaw = FMath::UnwindDegrees(AnchorYaw+YawVelocity*Step);
        FRotator Pose = GetActorRotation(); Pose.Yaw = NextYaw;
        // Engine translation sweeps do not sweep rotation. Verify the proposed hull pose too.
        if (ClearHullPose(Position,Pose,Query)) AnchorYaw = NextYaw;
        else YawVelocity *= .3f;
    }
    Anchor.X = Position.X; Anchor.Y = Position.Y;
}

void AMCCataclysmRaft::CollectRiders(TArray<AMCToothCharacter*>& Riders) const
{
    if (!bConfigured || !GetWorld()) return;
    for (const auto& Player:SeatPlayers)
        if (AMCToothCharacter* Hero=Player.Get(); IsValid(Hero) && Hero->SeatedRaft==this)
            Riders.Add(Hero);
}

void AMCCataclysmRaft::UpdateLoad(double Now)
{
    TArray<AMCToothCharacter*> Riders;
    CollectRiders(Riders);
    const int32 PreviousCount = OccupantCount;
    OccupantCount = Riders.Num();
    FVector Load = FVector::ZeroVector;
    for (const AMCToothCharacter* Hero : Riders)
        Load += Deck->GetComponentTransform().InverseTransformPosition(Hero->GetActorLocation());
    if (!Riders.IsEmpty()) Load /= Riders.Num();
    LoadPitch = FMath::Clamp(float(Load.X / 120.f) * 1.2f, -1.2f, 1.2f);
    LoadRoll = FMath::Clamp(float(Load.Y / 85.f) * -1.2f, -1.2f, 1.2f);
    if (SinkStartedAt >= 0. && Now >= SinkStartedAt + SubmergeSeconds)
    {
        SinkStartedAt = -1.;
        GraceEndsAt = Now + RepeatGrace;
        ForceNetUpdate();
    }
    if (!IsSubmerging())
    {
        if (OccupantCount <= Capacity && WarningStartedAt >= 0.)
        {
            WarningStartedAt = -1.; // Leaving the deck during the warning saves the raft.
            ForceNetUpdate();
        }
        else if (OccupantCount > Capacity && Now >= GraceEndsAt)
        {
            if (WarningStartedAt < 0.)
            {
                WarningStartedAt = Now;
                ForceNetUpdate();
            }
            else if (Now >= WarningStartedAt + WarningSeconds)
            {
                SinkStartedAt = Now;
                WarningStartedAt = -1.;
                ForceNetUpdate();
            }
        }
    }
    if (PreviousCount != OccupantCount) ForceNetUpdate();
}

float AMCCataclysmRaft::SinkAmount(double Now) const
{
    if (SinkStartedAt < 0.) return 0.f;
    const float Age = float(Now - SinkStartedAt);
    if (Age < 0.f || Age >= SubmergeSeconds) return 0.f;
    if (Age < .65f) return FMath::SmoothStep(0.f, 1.f, Age / .65f);
    if (Age < 1.35f) return 1.f;
    return 1.f - FMath::SmoothStep(0.f, 1.f, (Age - 1.35f) / .65f);
}

void AMCCataclysmRaft::UpdatePose(float DeltaSeconds, double Now)
{
    Deck->SetVisibility(bConfigured);
    Deck->SetCollisionEnabled(bConfigured ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    if (!bConfigured) return;
    if (!HasAuthority()) return; // RepMovement owns the walkable base on every remote peer.
    const FVector PreviousAnchor = Anchor;
    UpdateDrift(DeltaSeconds, Now);
    const float Water = Flood && Flood->IsActive() ? Flood->SurfaceHeightAt(Anchor) + WaterlineOffset : SurfaceWorldZ;
    const float Surface = FMath::Max(float(Anchor.Z) + FloatDraft, Water);
    const float Wetness = FMath::Clamp((Surface - float(Anchor.Z) - FloatDraft) / 15.f, 0.f, 1.f);
    const float Age = float(Now - FloatStartedAt);
    const float Sink = SinkAmount(Now);
    const float Bob = 2.5f * FMath::Sin(Age * 1.5f) * Wetness * (1.f - Sink);
    float WavePitch = 0.f, WaveRoll = 0.f;
    if (Flood && Flood->IsActive() && Flood->IsCataclysmStorm())
    {
        const FQuat Heading = FRotator(0,AnchorYaw,0).Quaternion();
        const FVector Forward = Heading.GetForwardVector()*100.f;
        const FVector Right = Heading.GetRightVector()*75.f;
        WavePitch = -FMath::RadiansToDegrees(FMath::Atan((Flood->SurfaceHeightAt(Anchor+Forward)-Flood->SurfaceHeightAt(Anchor-Forward))/200.f));
        WaveRoll = FMath::RadiansToDegrees(FMath::Atan((Flood->SurfaceHeightAt(Anchor+Right)-Flood->SurfaceHeightAt(Anchor-Right))/150.f));
    }
    const float Pitch = FMath::Clamp(LoadPitch + .6f*FMath::Sin(Age*.9f) + WavePitch + ImpactPitch, -5.f, 5.f)*Wetness;
    const float Roll = FMath::Clamp(LoadRoll + .6f*FMath::Sin(Age*1.1f+.7f) + WaveRoll + ImpactRoll, -5.f, 5.f)*Wetness;
    if (!bPoseInitialized || DeltaSeconds <= 0.f) { VisualPitch = Pitch; VisualRoll = Roll; }
    else { VisualPitch = FMath::FInterpTo(VisualPitch, Pitch, DeltaSeconds, 5.f); VisualRoll = FMath::FInterpTo(VisualRoll, Roll, DeltaSeconds, 5.f); }
    // A shallow intro wave can beach the raft, never push its floats through the tongue.
    const float FloatingClearance = FMath::Max(0.f, Surface - float(Anchor.Z) - FloatDraft);
    const float ActualSink = FMath::Min(SinkDepth * Sink, FloatingClearance);
    const float TiltClearance = FMath::Clamp((FloatingClearance - ActualSink) / 25.f, 0.f, 1.f);
    const FVector Previous = GetActorLocation();
    FVector Location(Anchor.X, Anchor.Y, FMath::Max(float(Anchor.Z)+FloatDraft, Surface+Bob-ActualSink));
    FRotator Rotation((VisualPitch+3.f*Sink)*TiltClearance, AnchorYaw,(VisualRoll+4.f*Sink)*TiltClearance);
    // Vertical float/sink motion still cannot put the hull inside a raised gum.
    // The unconfigured initialization has no previous legal pose to fall back to.
    if (bPoseInitialized && DeltaSeconds > 0.f)
    {
        const FCollisionQueryParams Query = TravelQuery();
        if (!ClearHullPose(Location,Rotation,Query))
        {
            Rotation = FRotator(0,AnchorYaw,0);
            if (!ClearHullPose(Location,Rotation,Query))
            {
                Location = Previous;
                Rotation = GetActorRotation();
                Anchor = PreviousAnchor;
                LastSupportPoint = PreviousAnchor;
                bHasSupportPoint = false;
                NextSupportScanAt = 0.;
                HorizontalVelocity *= .3f;
            }
        }
    }
    SetActorLocationAndRotation(Location,Rotation,false);
    Deck->ComponentVelocity = bPoseInitialized && DeltaSeconds > UE_SMALL_NUMBER
        ? (GetActorLocation() - Previous) / DeltaSeconds : FVector::ZeroVector;
    bPoseInitialized = true;
}

bool AMCCataclysmRaft::FindBoardingPoint(const AMCToothCharacter* Hero, FVector& DeckFootPoint) const
{
    if (!Hero || !IsBoardable() || !Hero->Status || !Hero->Status->IsAlive()) return false;
    const int32 Slot=SeatPlayers.IndexOfByPredicate([](const auto& Player) { return !IsValid(Player.Get()); });
    if (Slot==INDEX_NONE) return false;
    const FVector Point=SeatFootPoint(Slot);
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCCataclysmBoard),false,Hero);
    if (!Deck->LineTraceComponent(Hit,Point+FVector(0,0,25),Point-FVector(0,0,25),Params)
        || Hit.ImpactNormal.Z<.9f) return false;
    DeckFootPoint=Hit.ImpactPoint;
    return true;
}

void AMCCataclysmRaft::InitializeCues()
{
    if (GetNetMode() == NM_DedicatedServer) return;
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Cataclysm/Raft/M_RaftCue.M_RaftCue"));
    if (!Material) return;
    for (UProceduralMeshComponent* Cue : {WarningOutline.Get(), WaterWake.Get()}) Cue->SetMaterial(0, Material);
    Ring(WarningOutline, 124.f, 88.f, 2.f, DeckTop + 1.5f, 0.f, FLinearColor(1.f, .2f, .015f, 0.f), true);
    Ring(WaterWake, 143.f, 103.f, 4.f, 0.f, 0.f, FLinearColor(.48f, .62f, .68f, .35f), true);
}

void AMCCataclysmRaft::UpdateOars(double Now)
{
    // Two fixed visual components reuse the character's real brush. The inventory
    // keeps that component's brush mesh even when a pickaxe/tool slot is selected.
    const FTransform DeckPose = Deck->GetComponentTransform();
    const float Dt=LastBrushVisualAt>=0 ? FMath::Clamp(float(Now-LastBrushVisualAt),0.f,.1f) : 0.f;
    LastBrushVisualAt=Now;
    for (int32 Side=0; Side<Capacity; ++Side)
    {
        UStaticMeshComponent* Oar = Side==0 ? LeftOar.Get() : RightOar.Get();
        AMCToothCharacter* Hero = RowingPlayers.IsValidIndex(Side) ? RowingPlayers[Side].Get() : nullptr;
        const UStaticMeshComponent* Brush = IsValid(Hero) ? Hero->Brush.Get() : nullptr;
        const bool Show = bConfigured && Brush && Brush->GetStaticMesh() && RowingCount>0 && !IsSubmerging();
        Oar->SetVisibility(Show);
        if (!Show) {VisualRowers[Side].Reset();continue;}
        FVector DesiredRow=RowingDirectionFor(Hero).GetSafeNormal2D();
        if(DesiredRow.IsNearlyZero()) DesiredRow=Hero->GetActorForwardVector().GetSafeNormal2D();
        if (Oar->GetStaticMesh() != Brush->GetStaticMesh())
        {
            Oar->EmptyOverrideMaterials();
            Oar->SetStaticMesh(Brush->GetStaticMesh());
        }
        for (int32 Slot=0; Slot<Brush->GetNumMaterials(); ++Slot)
            if (Oar->GetMaterial(Slot) != Brush->GetMaterial(Slot)) Oar->SetMaterial(Slot,Brush->GetMaterial(Slot));
        const FVector BrushScale = Brush->GetComponentScale();
        Oar->SetWorldScale3D(BrushScale);
        const FVector Rider = DeckPose.InverseTransformPosition(Hero->GetActorLocation());
        // Choose the nearby water side in the requested stroke frame. The
        // stroke can point across or behind the hull while slots stay fixed.
        const FVector DesiredLocalRow=DeckPose.InverseTransformVectorNoScale(DesiredRow).GetSafeNormal2D();
        const FVector DesiredAcross=FVector::CrossProduct(FVector::UpVector,DesiredLocalRow).GetSafeNormal();
        const float AcrossPosition=float(FVector::DotProduct(Rider,DesiredAcross));
        const float DesiredSide=AcrossPosition>12.f?1.f:AcrossPosition<-12.f?-1.f:(Side==0?-1.f:1.f);
        if(VisualRowers[Side].Get()!=Hero) {
            VisualRowers[Side]=Hero;VisualPaddleSide[Side]=DesiredSide;VisualRowingDirection[Side]=DesiredRow;
        }
        else {
            VisualPaddleSide[Side]=FMath::FInterpConstantTo(VisualPaddleSide[Side],DesiredSide,Dt,6.f);
            const float VisualYaw=FMath::FixedTurn(VisualRowingDirection[Side].Rotation().Yaw,DesiredRow.Rotation().Yaw,220.f*Dt);
            VisualRowingDirection[Side]=FRotator(0,VisualYaw,0).Vector();
        }
        // One bounded cosmetic direction drives both the shaft and the torso.
        // Authority applies the requested force immediately; only pose motion eases.
        const FVector LocalRow=DeckPose.InverseTransformVectorNoScale(VisualRowingDirection[Side]).GetSafeNormal2D();
        const FVector Across=FVector::CrossProduct(FVector::UpVector,LocalRow).GetSafeNormal();
        const float Sign=VisualPaddleSide[Side];
        const float Crossing=FMath::Sin((Sign+1.f)*UE_HALF_PI);
        const MCRowingStroke::FSample Stroke = MCRowingStroke::Sample(RowingStrokePhase(Hero));
        const float Lift=FMath::Max(Stroke.Lift,Crossing);
        // Reach upstream, catch, sweep the water opposite the chosen travel,
        // then lift the brush for its return. The character uses these exact
        // stages rather than a second, unsynchronised sine animation.
        const FVector Direction = (Across*(Sign*.62f) + FVector(0,0,-(.91f-.36f*Lift))
            + LocalRow*(.50f*Stroke.Reach)).GetSafeNormal();
        const FVector Contact = Hero->Inventory ? Hero->Inventory->BrushContactLocal() : FVector(72,0,-20);
        const float Length = float((Contact*BrushScale).Size());
        // Match the actual short brush. Do not enlarge it into a new long oar or
        // stretch the shaft; a small downward stroke uses nearby hands.
        const float GripHeight = FMath::Clamp(Length*.85f,DeckTop+24.f,DeckTop+48.f);
        const FVector Grip = FVector(Rider.X,Rider.Y,GripHeight+15.f*Lift-3.f*Stroke.Power)
            + LocalRow*(23.f*Stroke.Reach) + Across*(Sign*28.f)
            + DeckPose.InverseTransformVectorNoScale(Hero->GetActorForwardVector()).GetSafeNormal2D()*(48.f*Crossing);
        // A horizontal side hint cannot become parallel to the downward shaft,
        // including its vertical midpoint when switching sides. Up was singular
        // there and could abruptly flip the brush and both wrists.
        Oar->SetRelativeLocationAndRotation(Grip,
            FRotationMatrix::MakeFromXY(Direction,Across).Rotator());
    }
}

void AMCCataclysmRaft::UpdateCues(double Now)
{
    if (GetNetMode() == NM_DedicatedServer || !bConfigured || !WaterWake->GetNumSections()) return;
    const bool Warning = WarningStartedAt >= 0. || IsSubmerging();
    WarningOutline->SetVisibility(Warning);
    if (Warning)
    {
        const float Blink = .55f + .45f * FMath::Sin(float(Now) * 14.f);
        Ring(WarningOutline, 124.f, 88.f, 3.f, DeckTop + 1.5f, float(Now),
            FLinearColor(1.25f, .05f + .18f * Blink, .008f, .45f + .5f * Blink), false);
    }
    const float Water = Flood && Flood->IsActive() ? Flood->SurfaceHeightAt(Anchor) + WaterlineOffset : SurfaceWorldZ;
    const bool Wet = Water > float(Anchor.Z) + FloatDraft + 3.f;
    WaterWake->SetVisibility(Wet);
    if (Wet)
    {
        const float Sink = SinkAmount(Now);
        const float Expansion = 1.f + .16f * Sink;
        WaterWake->SetWorldLocationAndRotation(FVector(GetActorLocation().X, GetActorLocation().Y, Water + 1.8f), FRotator(0.f, GetActorRotation().Yaw, 0.f));
        Ring(WaterWake, 143.f * Expansion, 103.f * Expansion, 3.5f + 3.f * Sink, 0.f, float(Now) * 2.f,
            FLinearColor(.44f, .59f, .65f, .22f + .38f * Sink), false);
    }
}

void AMCCataclysmRaft::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = Clock();
    if (HasAuthority() && bConfigured)
    {
        ValidateSeats();
        if (Flood && Flood->IsActive()) UpdateSurface(Flood->SurfaceHeightAt(Anchor));
        if (Now >= NextLoadScanAt) { UpdateLoad(Now); NextLoadScanAt = Now + .05; }
        UpdateRowing(Now);
    }
    UpdatePose(DeltaSeconds, Now);
    UpdateCues(Now);
    UpdateOars(Now);
}

void AMCCataclysmRaft::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCCataclysmRaft, Flood);
    DOREPLIFETIME(AMCCataclysmRaft, bConfigured);
    DOREPLIFETIME(AMCCataclysmRaft, Anchor);
    DOREPLIFETIME(AMCCataclysmRaft, AnchorYaw);
    DOREPLIFETIME(AMCCataclysmRaft, SurfaceWorldZ);
    DOREPLIFETIME(AMCCataclysmRaft, WaterlineOffset);
    DOREPLIFETIME(AMCCataclysmRaft, FloatStartedAt);
    DOREPLIFETIME(AMCCataclysmRaft, OccupantCount);
    DOREPLIFETIME(AMCCataclysmRaft, SeatPlayers);
    DOREPLIFETIME(AMCCataclysmRaft, WarningStartedAt);
    DOREPLIFETIME(AMCCataclysmRaft, SinkStartedAt);
    DOREPLIFETIME(AMCCataclysmRaft, LoadPitch);
    DOREPLIFETIME(AMCCataclysmRaft, LoadRoll);
    DOREPLIFETIME(AMCCataclysmRaft, RowingCount);
    DOREPLIFETIME(AMCCataclysmRaft, RowingDirection);
    DOREPLIFETIME(AMCCataclysmRaft, RowingPlayers);
    DOREPLIFETIME(AMCCataclysmRaft, RowingDirections);
    DOREPLIFETIME(AMCCataclysmRaft, RowingStartedAt);
}
