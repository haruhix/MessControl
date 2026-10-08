#include "MCTongue.h"
#include "MCFoodActor.h"
#include "EngineUtils.h"

namespace
{
struct FSpawnBands
{
    FBox Bounds;
    double Near=0,Split=0,Far=0;

    explicit FSpawnBands(const AMCTongue& Tongue)
        : Bounds(Tongue.Surface->Bounds.GetBox())
    {
        const auto Finite=[](float Value,float Default) {return FMath::IsFinite(Value)?Value:Default;};
        const double NearDepth=FMath::Clamp(double(Finite(Tongue.GameplaySpawnNearDepth,.18f)),0.,.95);
        const double FarDepth=FMath::Clamp(double(Finite(Tongue.GameplaySpawnFarDepth,.82f)),NearDepth+.05,1.);
        const double SplitDepth=FMath::Clamp(double(Finite(Tongue.GameplaySpawnSplitDepth,.60f)),NearDepth+.01,FarDepth-.01);
        Near=Bounds.Max.X-Bounds.GetSize().X*NearDepth;
        Split=Bounds.Max.X-Bounds.GetSize().X*SplitDepth;
        Far=Bounds.Max.X-Bounds.GetSize().X*FarDepth;
    }

    bool IsValid() const {return Bounds.IsValid && Bounds.GetSize().X>KINDA_SMALL_NUMBER && Bounds.GetSize().Y>KINDA_SMALL_NUMBER;}
};

bool CircleTouchesPolygon(FVector Point,double Radius,TConstArrayView<FVector> Polygon)
{
    if(Polygon.Num()<3) return false;
    const FVector2D P(Point.X,Point.Y);
    FVector2D A(Polygon.Last().X,Polygon.Last().Y);
    bool Inside=false;
    for(const FVector& Vertex:Polygon)
    {
        const FVector2D B(Vertex.X,Vertex.Y),Segment=B-A;
        if((A.Y>P.Y)!=(B.Y>P.Y) && P.X<(B.X-A.X)*(P.Y-A.Y)/(B.Y-A.Y)+A.X) Inside=!Inside;
        const double Alpha=FMath::Clamp(FVector2D::DotProduct(P-A,Segment)/FMath::Max(Segment.SizeSquared(),1.e-12),0.,1.);
        if((P-(A+Segment*Alpha)).SizeSquared()<=Radius*Radius+.01) return true;
        A=B;
    }
    return Inside;
}

bool TouchesDeliveryLane(const AMCTongue& Tongue,FVector Point,float Margin,bool AllowBrushBin=false)
{
    for(TActorIterator<AMCFoodDisposal> It(Tongue.GetWorld());It;++It)
    {
        if(AllowBrushBin && It->bBrushBin) continue;
        TArray<FVector> Outer,Inner,Polygon;
        if(It->GetDeliveryZoneOutline(Outer,Inner))
        {
            Polygon=MoveTemp(Outer);
            for(int32 I=Inner.Num()-1;I>=0;--I) Polygon.Add(Inner[I]);
        }
        else
        {
            FTransform Geometry;FVector Extent;bool Circular;
            It->GetDeliveryZoneGeometry(Geometry,Extent,Circular);
            // If the native silhouette is unavailable, conservatively keep the
            // full receiver box clear, including circular receiver corners.
            for(const FVector Corner:{FVector(-1,-1,0),FVector(1,-1,0),FVector(1,1,0),FVector(-1,1,0)})
                Polygon.Add(Geometry.TransformPosition(Corner*Extent));
        }
        if(CircleTouchesPolygon(Point,Margin,Polygon)) return true;
    }
    return false;
}
}

int32 AMCTongue::GameplaySpawnZone(FVector Point) const
{
    const FSpawnBands Bands(*this);
    if(Point.ContainsNaN() || !Bands.IsValid() || Point.X>Bands.Near || Point.X<Bands.Far
        || Point.Y<Bands.Bounds.Min.Y || Point.Y>Bands.Bounds.Max.Y) return INDEX_NONE;
    return Point.X>Bands.Split?0:1;
}

bool AMCTongue::GameplaySpawnFootprint(FVector Point,float Margin,FHitResult& Hit) const
{
    if(!FMath::IsFinite(Margin) || Margin<0 || GameplaySpawnZone(Point)==INDEX_NONE) return false;
    const FSpawnBands Bands(*this);
    // Objects may straddle the internal 30/70 boundary, but their entire circular
    // footprint must stay between the two forbidden outer strips.
    if(Point.X+Margin>Bands.Near || Point.X-Margin<Bands.Far
        || !InteriorSurfacePoint(Point,Margin,Hit)) return false;
    return !TouchesDeliveryLane(*this,Hit.ImpactPoint,Margin);
}

bool AMCTongue::GameplayStuckFoodFootprint(FVector Point,float Margin,FHitResult& Hit) const
{
    if(!FMath::IsFinite(Margin) || Margin<0 || GameplaySpawnZone(Point)==INDEX_NONE) return false;
    const FSpawnBands Bands(*this);
    if(Point.X+Margin>Bands.Near || Point.X-Margin<Bands.Far
        || !InteriorSurfacePoint(Point,Margin,Hit)) return false;
    // Only directed, tooth-anchored Stuck food uses this query. Its native
    // delivery phase guard prevents disposal while the players work on it.
    return !TouchesDeliveryLane(*this,Hit.ImpactPoint,Margin,true);
}

bool AMCTongue::RandomGameplaySpawnPoint(FRandomStream& Random,float Margin,float Separation,TConstArrayView<FVector> Excluded,FHitResult& Hit,int32* OutZone)
{
    if(OutZone) *OutZone=INDEX_NONE;
    if(!HasAuthority() || Positions.IsEmpty() || !FMath::IsFinite(Margin) || Margin<0
        || !FMath::IsFinite(Separation) || Separation<0) return false;
    const FSpawnBands Bands(*this);
    if(!Bands.IsValid() || Bands.Bounds.GetSize().Y<=Margin*2 || Bands.Near-Bands.Far<=Margin*2) return false;
    const float LeftChance=FMath::IsFinite(GameplaySpawnLeftChance)?FMath::Clamp(GameplaySpawnLeftChance,0.f,1.f):.30f;
    // Pick the band once. Resampling the band after a blocked candidate would
    // turn 30/70 into an area- or congestion-dependent distribution.
    const int32 Zone=Random.FRand()<LeftChance?0:1;
    const double MinX=Zone==0?FMath::Max(Bands.Split,Bands.Far+Margin):Bands.Far+Margin;
    const double MaxX=Zone==0?Bands.Near-Margin:FMath::Min(Bands.Split,Bands.Near-Margin);
    if(MinX>=MaxX) return false;
    for(int32 Attempt=0;Attempt<128;++Attempt)
    {
        const FVector Candidate(Random.FRandRange(MinX,MaxX),
            Random.FRandRange(Bands.Bounds.Min.Y+Margin,Bands.Bounds.Max.Y-Margin),Bands.Bounds.GetCenter().Z);
        if(GameplaySpawnZone(Candidate)!=Zone) continue;
        bool Near=false;
        for(const FVector& P:Excluded) Near|=FVector::DistSquared2D(Candidate,P)<FMath::Square(Separation);
        for(const FVector& P:RecentSpawnPoints) Near|=FVector::DistSquared2D(Candidate,P)<FMath::Square(FMath::Min(Separation,160.f));
        if(Near || !GameplaySpawnFootprint(Candidate,Margin,Hit)) continue;
        RecentSpawnPoints.Add(Hit.ImpactPoint);
        if(RecentSpawnPoints.Num()>32) RecentSpawnPoints.RemoveAt(0,RecentSpawnPoints.Num()-32);
        if(OutZone) *OutZone=Zone;
        return true;
    }
    // An unavailable band never falls back into a delivery lane or the other band.
    return false;
}
