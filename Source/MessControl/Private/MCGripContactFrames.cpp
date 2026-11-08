#include "MCGripComponent.h"
#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "Engine/HitResult.h"

void UMCGripComponent::ResolveCanonicalContact(bool Left,FVector& Point,FVector& Normal) const
{
    const auto* F=HandFrame(Left);
    if(!F) {Point=Tooth?Tooth->GetActorLocation():FVector::ZeroVector;Normal=FVector::ForwardVector;return;}
    const FTransform T=F->Food->GetCanonicalModelTransform();const float Lift=LiftAlpha(F->Food);
    const FVector Original=Left?FVector(F->LeftPoint):FVector(F->RightPoint);
    const FVector OldNormal=Left?FVector(F->LeftNormal):FVector(F->RightNormal);
    Normal=T.TransformVectorNoScale(FMath::Lerp(OldNormal,FVector(F->LiftNormal),Lift)).GetSafeNormal();
    Point=T.TransformPosition(FMath::Lerp(Original,FVector(F->LiftPoint),Lift));
    if(Lift>0 && Lift<1) {
        FHitResult Hit;
        if(F->Food->FindGripSurface(Point+Normal*F->Food->GetCanonicalModelBounds().GetExtent().Size()*2,Hit)) Point=Hit.ImpactPoint;
    }
}
FVector UMCGripComponent::CanonicalContactPoint(bool Left) const
{
    if(IsBracing()) return BracePoint()+FVector::CrossProduct(FVector::UpVector,BraceNormal()).GetSafeNormal()*(Left?-8:8);
    if(IsValid(GrabbedPlayer)) return GrabbedPlayer->GetSimulationMeshSocketTransform(GrabbedPlayer->RigBone(TEXT("body"))).TransformPosition(PlayerAnchor)
        +Tooth->GetActorRightVector()*(Left?-10:10);
    FVector P,N;ResolveCanonicalContact(Left,P,N);return P;
}
