#include "MCRewardDropZone.h"
#include "MCTongue.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AMCRewardDropZone::AMCRewardDropZone()
{
    PrimaryActorTick.bCanEverTick=false;
    Area=CreateDefaultSubobject<UBoxComponent>(TEXT("AllowedDropArea"));
    SetRootComponent(Area);
    Area->SetBoxExtent(FVector(550,550,300));
    Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Area->SetHiddenInGame(true);
    Area->ShapeColor=FColor(100,220,140);
}

bool AMCRewardDropZone::AcceptsFloor(const FHitResult& Hit) const
{
    AActor* Floor=Hit.GetActor();
    const UPrimitiveComponent* Component=Hit.GetComponent();
    if(!Hit.bBlockingHit || !IsValid(Floor) || Floor->IsHidden() || Floor->IsActorBeingDestroyed()
        || !Component || !Component->IsVisible() || !Component->IsRegistered()
        || Hit.ImpactNormal.Z<.85f) return false;
    return AllowedLandingSurface ? Floor==AllowedLandingSurface : Floor->IsA<AMCTongue>();
}

bool AMCRewardDropZone::ContainsFootprint(FVector Point,FVector HalfExtent) const
{
    if(!bEnabled || !Area || Point.ContainsNaN() || HalfExtent.ContainsNaN()) return false;
    const FTransform Space=Area->GetComponentTransform();
    const FVector P=Space.InverseTransformPosition(Point),Size=Area->GetUnscaledBoxExtent();
    const FVector Scale=Space.GetScale3D().GetAbs();
    if(Scale.GetMin()<.001f || !FMath::IsFinite(InwardMargin)) return false;
    // Conservative enclosing world footprint, including rotation of the authored box.
    const FVector X=Space.GetUnitAxis(EAxis::X).GetAbs(),Y=Space.GetUnitAxis(EAxis::Y).GetAbs();
    const double PaddingX=(FVector::DotProduct(X,HalfExtent.GetAbs())+FMath::Max(0.f,InwardMargin))/Scale.X;
    const double PaddingY=(FVector::DotProduct(Y,HalfExtent.GetAbs())+FMath::Max(0.f,InwardMargin))/Scale.Y;
    return FMath::Abs(P.X)+PaddingX<=Size.X && FMath::Abs(P.Y)+PaddingY<=Size.Y && FMath::Abs(P.Z)<=Size.Z;
}

bool AMCRewardDropZone::FindLanding(FRandomStream& Random,FVector HalfExtent,float FallHeight,
    TConstArrayView<AActor*> Ignored,FVector& OutFloor,FVector& OutNormal) const
{
    if(!GetWorld() || !Area || !bEnabled || !FMath::IsFinite(FallHeight) || FallHeight<1
        || HalfExtent.ContainsNaN() || HalfExtent.GetMin()<=0) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RewardDropGround),true,this);
    for(AActor* Actor:Ignored) if(Actor) Query.AddIgnoredActor(Actor);
    const FTransform Space=Area->GetComponentTransform();
    const FVector Scale=Space.GetScale3D().GetAbs();
    if(Scale.GetMin()<.001f || !FMath::IsFinite(InwardMargin)) return false;
    const FVector Bounds=Area->GetUnscaledBoxExtent();
    const FVector XAxis=Space.GetUnitAxis(EAxis::X).GetAbs(),YAxis=Space.GetUnitAxis(EAxis::Y).GetAbs();
    const double AvailableX=Bounds.X-(FVector::DotProduct(XAxis,HalfExtent)+FMath::Max(0.f,InwardMargin))/Scale.X;
    const double AvailableY=Bounds.Y-(FVector::DotProduct(YAxis,HalfExtent)+FMath::Max(0.f,InwardMargin))/Scale.Y;
    if(AvailableX<=0 || AvailableY<=0) return false;
    const FVector Local(Random.FRandRange(-AvailableX,AvailableX),Random.FRandRange(-AvailableY,AvailableY),0);
    const FVector Candidate=Space.TransformPosition(Local);
    const FBox Box=Area->Bounds.GetBox();
    const auto Reject=[this,Candidate,HalfExtent](const TCHAR* Reason,const FHitResult* Hit=nullptr,double Variation=0.) {
        if(FParse::Param(FCommandLine::Get(),TEXT("MCRoguelikePreview"))) {
            static TMap<FString,double> LastReport;
            const FString Key=GetName()+Reason;
            double* Last=LastReport.Find(Key);
            const double Now=GetWorld()->GetTimeSeconds();
            if(!Last || Now-*Last>=10.) {
                LastReport.Add(Key,Now);
                const AActor* Actor=Hit?Hit->GetActor():nullptr;
                const UPrimitiveComponent* Component=Hit?Hit->GetComponent():nullptr;
                UE_LOG(LogTemp,Display,TEXT("MC_REWARD_REJECT zone=%s reason=%s candidate=%s extent=%s actor=%s component=%s visible=%d hidden=%d normalZ=%.3f variation=%.2f"),
                    *GetName(),Reason,*Candidate.ToString(),*HalfExtent.ToString(),*GetNameSafe(Actor),*GetNameSafe(Component),
                    Component?Component->IsVisible():0,Actor?Actor->IsHidden():0,Hit?Hit->ImpactNormal.Z:0.,Variation);
            }
        }
        return false;
    };
    FHitResult Center;
    if(!GetWorld()->LineTraceSingleByChannel(Center,FVector(Candidate.X,Candidate.Y,Box.Max.Z+40),
        FVector(Candidate.X,Candidate.Y,Box.Min.Z-40),ECC_Visibility,Query)) return Reject(TEXT("NoFloor"));
    if(!AcceptsFloor(Center)) return Reject(TEXT("WrongFloor"),&Center);
    if(!ContainsFootprint(Center.ImpactPoint,HalfExtent)) return Reject(TEXT("Footprint"),&Center);
    float Highest=float(Center.ImpactPoint.Z),MaxDeviation=0,Raise=0;
    const FVector Normal=Center.ImpactNormal.GetSafeNormal();
    // Corners and edges must stay on the same actual support, including tongue boundaries.
    for(int32 X=-1;X<=1;++X) for(int32 Y=-1;Y<=1;++Y) {
        FHitResult Floor;
        const FVector P=Center.ImpactPoint+FVector(X*HalfExtent.X,Y*HalfExtent.Y,0);
        if(!GetWorld()->LineTraceSingleByChannel(Floor,FVector(P.X,P.Y,Box.Max.Z+40),
            FVector(P.X,P.Y,Box.Min.Z-40),ECC_Visibility,Query) || !AcceptsFloor(Floor)
            || Floor.GetActor()!=Center.GetActor() || !ContainsFootprint(Floor.ImpactPoint,FVector::ZeroVector)) return Reject(TEXT("Support"),&Floor);
        Highest=FMath::Max(Highest,float(Floor.ImpactPoint.Z));
        const FVector Delta=Floor.ImpactPoint-Center.ImpactPoint;
        const float Residual=float(Delta.Z+(Normal.X*Delta.X+Normal.Y*Delta.Y)/Normal.Z);
        MaxDeviation=FMath::Max(MaxDeviation,FMath::Abs(Residual)); Raise=FMath::Max(Raise,Residual);
    }
    if(!FMath::IsFinite(MaxFloorVariation) || MaxDeviation>FMath::Clamp(MaxFloorVariation,0.f,30.f)) return Reject(TEXT("Variation"),&Center,MaxDeviation);
    Query.bTraceComplex=false;
    Query.AddIgnoredActor(Center.GetActor());
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    Objects.AddObjectTypesToQuery(ECC_Pawn); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    const FVector Floor(Center.ImpactPoint.X,Center.ImpactPoint.Y,Highest+3);
    const FVector End=Floor+FVector(0,0,HalfExtent.Z),Start=End+FVector(0,0,FallHeight);
    const FCollisionShape Shape=FCollisionShape::MakeBox(HalfExtent);
    if(GetWorld()->OverlapAnyTestByObjectType(End,FQuat::Identity,Objects,Shape,Query)) {
        if(FParse::Param(FCommandLine::Get(),TEXT("MCRoguelikePreview"))) {
            TArray<FOverlapResult> Overlaps;
            GetWorld()->OverlapMultiByObjectType(Overlaps,End,FQuat::Identity,Objects,Shape,Query);
            FHitResult Block;
            if(!Overlaps.IsEmpty()) Block=FHitResult(Overlaps[0].GetActor(),Overlaps[0].GetComponent(),End,FVector::ZeroVector);
            return Reject(TEXT("Occupied"),&Block);
        }
        return false;
    }
    FHitResult Obstacle;
    if(GetWorld()->SweepSingleByObjectType(Obstacle,Start,End,FQuat::Identity,Objects,Shape,Query)) return Reject(TEXT("FallBlocked"),&Obstacle);
    OutFloor=Center.ImpactPoint+FVector(0,0,Raise+3);
    OutNormal=Normal;
    return true;
}
