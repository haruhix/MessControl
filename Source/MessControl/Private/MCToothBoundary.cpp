#include "MCToothCharacter.h"
#include "MCGameMode.h"
#include "MCGameState.h"
#include "MCArenaTooth.h"
#include "MCThroat.h"
#include "MCTongue.h"
#include "MCTutorialDirector.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
constexpr double MouthEdgeClearance=350.;
constexpr double RowClearance=100.;
constexpr double BelowTongueClearance=300.;
constexpr double OutsideGraceSeconds=1.5;

FBox2D Footprint(const FBox& Box)
{
    return FBox2D(FVector2D(Box.Min),FVector2D(Box.Max));
}

bool NearTongueEdge(FVector2D Point,const TArray<FVector>& Vertices,const TArray<FIntPoint>& Edges)
{
    for(const FIntPoint& Edge:Edges)
    {
        if(!Vertices.IsValidIndex(Edge.X) || !Vertices.IsValidIndex(Edge.Y)) continue;
        const FVector2D A(Vertices[Edge.X]),B(Vertices[Edge.Y]),Segment=B-A;
        const double Along=FMath::Clamp(FVector2D::DotProduct(Point-A,Segment)/FMath::Max(1.e-9,Segment.SizeSquared()),0.,1.);
        if((Point-(A+Segment*Along)).SizeSquared()<=FMath::Square(MouthEdgeClearance)) return true;
    }
    return false;
}
}

void AMCToothCharacter::UpdateArenaBoundary()
{
    // A hub, a local wardrobe preview, an unpossessed corpse and an isolated
    // GameModeBase sandbox are not participants in the mouth arena.
    const auto* Mode=GetWorld()->GetAuthGameMode<AMCGameMode>();
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(!HasAuthority() || !Mode || !AMCGameMode::IsGameplayParticipant(GetController())
        || !Status || !Status->IsAlive() || bArenaArrivalHeld || SwallowedBy || IsMimicCaptured()
        || AMCTutorialDirector::IsSafeTutorial(GetWorld()) || (State && (State->bLobbyWaiting || State->bTutorialActive)))
    {
        OutsideMouthStartedAt=-1;
        return;
    }

    const bool Fallen=ToothPhysics && ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll;
    const FVector Point=Fallen?ToothPhysics->PhysicalLocation():GetActorLocation();
    if(Point.ContainsNaN()) {Status->KillFromArenaBoundary();return;}
    if(!BoundaryTongue.IsValid())
    {
        double Nearest=MAX_dbl;
        for(TActorIterator<AMCTongue> It(GetWorld());It;++It)
        {
            if(!It->Surface || !It->Surface->IsRegistered() || It->CurrentVertices().IsEmpty()) continue;
            const FBox Bounds=It->Surface->Bounds.GetBox();
            if(!Bounds.IsValid) continue;
            const double Distance=Bounds.ComputeSquaredDistanceToPoint(GetActorLocation());
            if(Distance<Nearest) {Nearest=Distance;BoundaryTongue=*It;}
        }
        BoundaryIndexCount=BoundaryVertexCount=INDEX_NONE;
        BoundaryRowFootprints.Reset();
    }
    auto* Tongue=BoundaryTongue.Get();
    // No authored playable surface means no guessed rectangle/height. The
    // engine's real KillZ still invokes FellOutOfWorld as the final fallback.
    if(!IsValid(Tongue)) {OutsideMouthStartedAt=-1;return;}
    if(BoundaryIndexCount!=Tongue->TriangleIndices().Num() || BoundaryVertexCount!=Tongue->CurrentVertices().Num())
    {
        BoundaryTongueEdges.Reset();
        BoundaryIndexCount=Tongue->TriangleIndices().Num();BoundaryVertexCount=Tongue->CurrentVertices().Num();
        // Only boundary edges need distance checks. Internal triangle edges
        // must not manufacture safe corridors beyond an irregular outline.
        TMap<uint64,int32> Counts;
        const auto& Indices=BoundaryTongue->TriangleIndices();
        for(int32 I=0;I+2<Indices.Num();I+=3) for(int32 J=0;J<3;++J)
        {
            const uint32 A=FMath::Min(Indices[I+J],Indices[I+(J+1)%3]);
            const uint32 B=FMath::Max(Indices[I+J],Indices[I+(J+1)%3]);
            ++Counts.FindOrAdd((uint64(A)<<32)|B);
        }
        for(const auto& Edge:Counts) if(Edge.Value==1)
            BoundaryTongueEdges.Add(FIntPoint(int32(Edge.Key>>32),int32(Edge.Key&0xffffffffu)));
        // A sealed artist mesh has no topological border. Its projected
        // silhouette still needs the same clearance: the nearest projected
        // triangle edge supplies that distance when the floor ray misses.
        if(BoundaryTongueEdges.IsEmpty()) for(const auto& Edge:Counts)
            BoundaryTongueEdges.Add(FIntPoint(int32(Edge.Key>>32),int32(Edge.Key&0xffffffffu)));
    }
    const FBox Bounds=Tongue->Surface->Bounds.GetBox();
    if(!Bounds.IsValid) return;

    // Independent of standing/ragdoll/armor and of how the map was translated.
    // The saved mouth's tongue itself reaches below world Z=-250.
    if(Point.Z<Bounds.Min.Z-BelowTongueClearance)
    {
        Status->KillFromArenaBoundary();
        return;
    }

    const FVector2D XY(Point);
    const FBox2D Tissue=Footprint(Bounds);
    if(State) for(const auto& Entry:State->ArenaTeeth)
    {
        auto* Tooth=Entry.Get();
        const TWeakObjectPtr<AMCArenaTooth> Key(Tooth);
        if(!IsValid(Tooth) || BoundaryRowFootprints.Contains(Key) || !Tooth->IsAvailable()
            || !Tooth->Body || Tooth->Body->IsSimulatingPhysics()) continue;
        const FBox Body=Tooth->Body->Bounds.GetBox();
        // In multi-tongue diagnostic worlds only this arena's registered row
        // belongs here. Save its intact footprint before a lost tooth flies off.
        if(Body.IsValid && Tissue.ExpandBy(2000.).IsInside(FVector2D(Body.GetCenter())))
            BoundaryRowFootprints.Add(Key,Footprint(Body).ExpandBy(RowClearance));
    }

    bool Inside=false;
    FHitResult Floor;
    if(Tissue.IsInside(XY)) Inside=Tongue->SurfacePoint(Point,Floor);
    if(!Inside && Tissue.ExpandBy(MouthEdgeClearance).IsInside(XY))
        Inside=NearTongueEdge(XY,Tongue->CurrentWorldVertices(),BoundaryTongueEdges);
    if(!Inside) for(const auto& Row:BoundaryRowFootprints)
        if(Row.Value.IsInside(XY)) {Inside=true;break;}
    if(!Inside) for(TActorIterator<AMCThroat> It(GetWorld());It;++It)
    {
        if(!Tissue.ExpandBy(2000.).IsInside(FVector2D(It->GetActorLocation()))) continue;
        // A real colliding uvula may be a legal platform. Neither the entire
        // back-wall AABB nor ContainsDeliveryPosition is safe: in manual intake
        // that method names the lethal aperture, not the front delivery strip.
        if(It->Uvula && It->Uvula->GetCollisionEnabled()!=ECollisionEnabled::NoCollision
            && It->Uvula->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block
            && Footprint(It->Uvula->Bounds.GetBox()).ExpandBy(RowClearance).IsInside(XY)) {Inside=true;break;}
    }

    if(Inside) {OutsideMouthStartedAt=-1;return;}
    const double Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    if(OutsideMouthStartedAt<0) OutsideMouthStartedAt=Now;
    // A short knockback/jump may cross an edge and recover. Returning really
    // resets the deadline; a body left on an outside ledge cannot live forever.
    if(Now-OutsideMouthStartedAt>=OutsideGraceSeconds) Status->KillFromArenaBoundary();
}
