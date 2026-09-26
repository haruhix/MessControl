#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "EngineUtils.h"

void FMCTonguePressureSettings::Sanitize()
{
    auto C=[](float V,float D,float A,float B){return FMath::Clamp(FMath::IsFinite(V)?V:D,A,B);};
    DepthPerKg=C(DepthPerKg,.4f,0,3); MaxDepth=C(MaxDepth,8,1,35);
    PlayerRadius=C(PlayerRadius,120,60,250); RagdollRadius=C(RagdollRadius,150,60,300); FoodMargin=C(FoodMargin,70,30,120);
    PressSeconds=C(PressSeconds,.12f,.06f,1); RecoverSeconds=C(RecoverSeconds,.6f,.15f,3);
    LandingBoost=C(LandingBoost,1,0,2); LandingSpeed=C(LandingSpeed,650,100,1200);
    ContactTolerance=C(ContactTolerance,12,3,20); MaxSources=FMath::Clamp(MaxSources,4,32);
}

bool AMCTongue::PressureSupport(AActor* Actor,FVector Center,float Bottom,FHitResult& Hit) const
{
    if (!SurfacePoint(Center,Hit) || Hit.ImpactNormal.Z<.4 || Bottom-Hit.ImpactPoint.Z>PressureSettings.ContactTolerance || Bottom-Hit.ImpactPoint.Z < -25) return false;
    // A floor, platform or another item between the source and the tongue carries the load instead.
    FCollisionQueryParams Params(SCENE_QUERY_STAT(MCTongueLoad),false,Actor);
    FHitResult Support;
    return GetWorld()->LineTraceSingleByChannel(Support,FVector(Center.X,Center.Y,FMath::Max(double(Bottom),Hit.ImpactPoint.Z)+25),Hit.ImpactPoint-FVector(0,0,5),ECC_Visibility,Params)
        && Support.GetComponent()==Surface;
}

void AMCTongue::GatherPressure(float Dt)
{
    const double Now=ServerTime(); CurrentLoads.Reset();
    auto Add=[&](AActor* Actor,EMCTongueLoadKind Kind,FVector P,FVector Axis,float RX,float RY,float Mass,float VZ,bool Supported)
    {
        if (!IsValid(Actor) || P.ContainsNaN() || !FMath::IsFinite(Mass) || !FMath::IsFinite(VZ)) return;
        FLoadHistory& H=LoadHistory.FindOrAdd(Actor); H.LastSeen=Now;
        if (Supported)
        {
            if (!H.bSupported && Now-H.LastContact>.25)
            {
                H.Impact=FMath::Clamp(FMath::Max(-VZ,H.DownSpeed)/PressureSettings.LandingSpeed,0.f,1.f)*PressureSettings.LandingBoost;
                H.LandingAt=Now;
            }
            H.LastContact=Now;
            FMCTongueLoad S; S.Actor=Actor; S.Kind=Kind; S.LocalPoint=GetActorTransform().InverseTransformPosition(P);
            S.Axis=Axis.GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
            S.RadiusX=FMath::Clamp(RX,60.f,300.f); S.RadiusY=FMath::Clamp(RY,60.f,300.f);
            S.Depth=FMath::Clamp(Mass,0.f,200.f)*PressureSettings.DepthPerKg*(1+H.Impact*FMath::Exp(-(Now-H.LandingAt)/.22));
            if (S.Depth>.001) CurrentLoads.Add(S);
        }
        H.bSupported=Supported; H.DownSpeed=FMath::Max(0.f,-VZ);
    };
    if (PressureSettings.bEnabled)
    {
        for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        {
            auto* Hero=*It; if (Hero->IsHidden()) continue;
            const auto* Physics=Hero->ToothPhysics.Get(); FHitResult Hit;
            if (Physics->GetBodyState()==EMCBodyState::Ragdoll)
            {
                if (const auto* Body=Hero->GetMesh()->GetBodyInstance(Hero->RigBone(TEXT("body"))))
                {
                    const FBox Bounds=Body->GetBodyBounds(); const FVector P=Bounds.GetCenter();
                    const bool Supported=!Hero->bInCoffee && !Hero->ClingTooth && PressureSupport(Hero,P,Bounds.Min.Z,Hit);
                    Add(Hero,EMCTongueLoadKind::Ragdoll,Supported?Hit.ImpactPoint:P,Hero->GetActorForwardVector(),PressureSettings.RagdollRadius,PressureSettings.RagdollRadius*.8f,Physics->Settings.Mass,Body->GetUnrealWorldVelocity().Z,Supported);
                }
            }
            else
            {
                const auto* Move=Hero->GetCharacterMovement(); const FVector P=Hero->GetActorLocation();
                const bool Supported=!Hero->bInCoffee && !Hero->ClingTooth && Move->IsMovingOnGround() && Move->CurrentFloor.HitResult.GetComponent()==Surface && SurfacePoint(P,Hit);
                Add(Hero,EMCTongueLoadKind::Player,Supported?Hit.ImpactPoint:P,Hero->GetActorForwardVector(),PressureSettings.PlayerRadius,PressureSettings.PlayerRadius,Physics->Settings.Mass,Move->Velocity.Z,Supported);
            }
        }
        // Reserve the limited footprint budget for players, then for the heaviest supported food.
        const int32 PlayerCount=CurrentLoads.Num();
        for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        {
            auto* Food=*It;
            if (Food->IsDisposed() || Food->IsHidden() || Food->Phase==EMCFoodPhase::Equipped || Food->Phase==EMCFoodPhase::Stuck || !Food->Body->IsSimulatingPhysics()) continue;
            const FBox Bounds=Food->Body->Bounds.GetBox(); const FVector P=Bounds.GetCenter(); FHitResult Hit;
            const bool Supported=PressureSupport(Food,P,Bounds.Min.Z,Hit);
            // World-aligned ellipse from the actual collider, not from a potentially offset visual mesh.
            Add(Food,EMCTongueLoadKind::Food,Supported?Hit.ImpactPoint:P,FVector::ForwardVector,Bounds.GetExtent().X+PressureSettings.FoodMargin,Bounds.GetExtent().Y+PressureSettings.FoodMargin,Food->Settings.Mass,Food->Body->GetPhysicsLinearVelocity().Z,Supported);
        }
        if (CurrentLoads.Num()>PlayerCount)
            MakeArrayView(CurrentLoads).Slice(PlayerCount,CurrentLoads.Num()-PlayerCount).Sort([](const FMCTongueLoad& A,const FMCTongueLoad& B)
            { return A.Depth==B.Depth?A.Actor->GetUniqueID()<B.Actor->GetUniqueID():A.Depth>B.Depth; });
        if (CurrentLoads.Num()>PressureSettings.MaxSources) CurrentLoads.SetNum(PressureSettings.MaxSources);
    }
    for (auto It=LoadHistory.CreateIterator();It;++It) if (!It.Key().IsValid() || Now-It.Value().LastSeen>2) It.RemoveCurrent();
    PressureSendElapsed+=Dt;
    if (PressureSendElapsed>=.1f)
    {
        PressureSendElapsed=0;
        if (!CurrentLoads.IsEmpty() || !PressureFrame.Sources.IsEmpty())
        { PressureFrame.Sources=CurrentLoads; PressureFrame.UpdatedAt=Now; ForceNetUpdate(); }
    }
}

void AMCTongue::UpdatePressureField(float Dt)
{
    const auto& Sources=PressureLoads(); const auto& S=PressureSettings;
    const bool Active=S.bEnabled && (HasAuthority() || ServerTime()-PressureFrame.UpdatedAt<1);
    const FTransform Transform=GetActorTransform(); const FVector Scale=Transform.GetScale3D();
    const float ZScale=FMath::Max(.01f,float(FMath::Abs(Scale.Z)));
    const FVector Size=RestBounds.GetSize();
    const float Press=1-FMath::Exp(-FMath::Min(Dt,.1f)/S.PressSeconds),Release=1-FMath::Exp(-FMath::Min(Dt,.1f)/S.RecoverSeconds);
    for (int32 I=0;I<Rest.Num();++I)
    {
        float Sum=0; FVector Gradient=FVector::ZeroVector;
        if (Active) for (const auto& Load:Sources)
        {
            const FVector D=Transform.TransformVector(Rest[I]-FVector(Load.LocalPoint));
            const FVector X=Load.Axis,Y=FVector::CrossProduct(FVector::UpVector,X);
            const float DX=FVector::DotProduct(D,X)/Load.RadiusX,DY=FVector::DotProduct(D,Y)/Load.RadiusY;
            const float R2=DX*DX+DY*DY; if (R2>=1) continue;
            const float K=1-R2; Sum+=Load.Depth*K*K*K;
            Gradient+=-6*Load.Depth*K*K*(X*(DX/Load.RadiusX)+Y*(DY/Load.RadiusY));
        }
        // Smooth saturation bounds overlapping dents and keeps their normals continuous.
        const float Denom=S.MaxDepth+Sum;
        Gradient*=FMath::Square(S.MaxDepth/Denom);
        const float Depth=S.MaxDepth*Sum/Denom/ZScale;
        const float U=(Rest[I].Z-RestBounds.Min.Z)/Size.Z;
        const float Top=FMath::SmoothStep(.15f,.75f,U),T=FMath::Clamp((U-.15f)/.6f,0.f,1.f);
        const float TopSlope=6*T*(1-T)/(.6f*Size.Z);
        const FVector G=Transform.InverseTransformVectorNoScale(Gradient)*Scale/ZScale*Top+FVector(0,0,Depth*TopSlope);
        const float Target=Depth*Top,Alpha=Target>IndentDepth[I]?Press:Release;
        IndentDepth[I]=FMath::Lerp(IndentDepth[I],Target,Alpha);
        IndentGradient[I]=FMath::Lerp(IndentGradient[I],G,Alpha);
        if (Target==0 && IndentDepth[I]<.001f) { IndentDepth[I]=0; IndentGradient[I]=FVector::ZeroVector; }
    }
}

float AMCTongue::IndentationAt(FVector P) const
{
    FHitResult Hit; if (!SurfacePoint(P,Hit) || Hit.FaceIndex<0 || Hit.FaceIndex*3+2>=Indices.Num()) return 0;
    const int32 A=Indices[Hit.FaceIndex*3],B=Indices[Hit.FaceIndex*3+1],C=Indices[Hit.FaceIndex*3+2];
    const FVector Local=GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
    const FVector Weights=FMath::GetBaryCentric2D(Local,Positions[A],Positions[B],Positions[C]);
    return (Weights.X*IndentDepth[A]*AnchorWeights[A]+Weights.Y*IndentDepth[B]*AnchorWeights[B]+Weights.Z*IndentDepth[C]*AnchorWeights[C])*FMath::Abs(GetActorScale3D().Z);
}
void AMCTongue::OnRep_PressureFrame()
{
    if (PressureEpoch==PressureFrame.Epoch) return;
    PressureEpoch=PressureFrame.Epoch; IndentDepth.Init(0,Rest.Num()); IndentGradient.Init(FVector::ZeroVector,Rest.Num());
}
void AMCTongue::ResetPressure()
{
    if (!HasAuthority()) return;
    CurrentLoads.Empty(); PressureFrame.Sources.Empty(); LoadHistory.Empty(); ++PressureFrame.Epoch;
    PressureFrame.UpdatedAt=ServerTime(); OnRep_PressureFrame(); ForceNetUpdate();
}
