#include "MCTonguePressure.h"
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
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void FMCTonguePressureSettings::Sanitize()
{
    auto C=[](float V,float D,float A,float B){return FMath::Clamp(FMath::IsFinite(V)?V:D,A,B);};
    DepthPerKg=C(DepthPerKg,.4f,0,3); MaxDepth=C(MaxDepth,8,1,35);
    FalloffPower=C(FalloffPower,3,2,6);
    PlayerDepthScale=C(PlayerDepthScale,1,0,3); RagdollDepthScale=C(RagdollDepthScale,1,0,3); FoodDepthScale=C(FoodDepthScale,1,0,3);
    PlayerWidthRatio=C(PlayerWidthRatio,1,.5f,1.5f); RagdollWidthRatio=C(RagdollWidthRatio,.8f,.5f,1.5f);
    PlayerRadius=C(PlayerRadius,120,60,250); RagdollRadius=C(RagdollRadius,150,60,300); FoodMargin=C(FoodMargin,70,30,120);
    PressSeconds=C(PressSeconds,.12f,.06f,1); RecoverSeconds=C(RecoverSeconds,.6f,.15f,5); TrailHoldSeconds=C(TrailHoldSeconds,0,0,1);
    LandingBoost=C(LandingBoost,1,0,2); LandingSpeed=C(LandingSpeed,650,100,1200);
    LandingSeconds=C(LandingSeconds,.22f,.08f,1);
    ContactTolerance=C(ContactTolerance,12,3,20); MaxSources=FMath::Clamp(MaxSources,4,32);
}

void AMCTongue::RefreshPressureMaterial()
{
    UMaterialInterface* Material=ActivePressureMaterial;
    if (!HasActorBegunPlay() && Profile && Profile->DefaultPressurePreset && Profile->DefaultPressurePreset->SurfaceMaterial)
        Material=Profile->DefaultPressurePreset->SurfaceMaterial;
    if (!Material) Material=SurfaceMaterial?SurfaceMaterial.Get():SourceMesh?SourceMesh->GetMaterial(0):nullptr;
    Surface->SetMaterial(0,Material);
}
void AMCTongue::ApplyPressurePreset(UMCTonguePressurePreset* Preset)
{
    if (!HasAuthority()) return;
    ActivePressurePreset=Preset;
    PressureSettings=Preset?Preset->Settings:Profile?Profile->Pressure:FMCTonguePressureSettings();
    PressureSettings.Sanitize();
    ActivePressureMaterial=Preset?Preset->SurfaceMaterial.Get():nullptr;
    RefreshPressureMaterial(); ForceNetUpdate();
}
void AMCTongue::ReloadPressureProfile() { ApplyPressurePreset(Profile?Profile->DefaultPressurePreset.Get():nullptr); }

// Fixed world-space bins restrict each load to nearby vertices. They rebuild only
// with the source mesh/actor transform; event displacement does not move vertices in XY.
namespace { constexpr float PressureCellSize=160.f; }
void AMCTongue::BuildPressureGrid()
{
    PressureCells.Empty(); PressureWorldVertices.SetNum(Rest.Num());
    PressureGridTransform=GetActorTransform();
    for(int32 I=0;I<Rest.Num();++I)
    {
        const FVector P=PressureGridTransform.TransformPosition(Rest[I]); PressureWorldVertices[I]=P;
        const float U=(Rest[I].Z-RestBounds.Min.Z)/FMath::Max(1.,RestBounds.GetSize().Z);
        if (U>.15f && AnchorWeights[I]>0)
            PressureCells.FindOrAdd(FIntPoint(FMath::FloorToInt(P.X/PressureCellSize),FMath::FloorToInt(P.Y/PressureCellSize))).Add(I);
    }
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
            const float Scale=Kind==EMCTongueLoadKind::Player?PressureSettings.PlayerDepthScale:Kind==EMCTongueLoadKind::Ragdoll?PressureSettings.RagdollDepthScale:PressureSettings.FoodDepthScale;
            S.Depth=FMath::Clamp(Mass,0.f,200.f)*PressureSettings.DepthPerKg*Scale*(1+H.Impact*FMath::Exp(-(Now-H.LandingAt)/PressureSettings.LandingSeconds));
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
                    Add(Hero,EMCTongueLoadKind::Ragdoll,Supported?Hit.ImpactPoint:P,Hero->GetActorForwardVector(),PressureSettings.RagdollRadius,PressureSettings.RagdollRadius*PressureSettings.RagdollWidthRatio,Physics->Settings.Mass,Body->GetUnrealWorldVelocity().Z,Supported);
                }
            }
            else
            {
                const auto* Move=Hero->GetCharacterMovement(); const FVector P=Hero->GetActorLocation();
                const bool Supported=!Hero->bInCoffee && !Hero->ClingTooth && Move->IsMovingOnGround() && Move->CurrentFloor.HitResult.GetComponent()==Surface && SurfacePoint(P,Hit);
                Add(Hero,EMCTongueLoadKind::Player,Supported?Hit.ImpactPoint:P,Hero->GetActorForwardVector(),PressureSettings.PlayerRadius,PressureSettings.PlayerRadius*PressureSettings.PlayerWidthRatio,Physics->Settings.Mass,Move->Velocity.Z,Supported);
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
    TRACE_CPUPROFILER_EVENT_SCOPE(MCTongue_PressureField);
    const auto& Sources=PressureLoads(); const auto& S=PressureSettings;
    const bool Active=S.bEnabled && (HasAuthority() || ServerTime()-PressureFrame.UpdatedAt<1);
    const FTransform Transform=GetActorTransform(); const FVector Scale=Transform.GetScale3D();
    if (!Transform.Equals(PressureGridTransform)) BuildPressureGrid();
    const float ZScale=FMath::Max(.01f,float(FMath::Abs(Scale.Z)));
    const FVector Size=RestBounds.GetSize();
    // Exponential response is stable for large steps. Capping Dt made trails
    // recover several times slower in a slow frame/capture than in ordinary Play.
    const float Step=FMath::Max(0.f,Dt);
    const float Press=1-FMath::Exp(-Step/S.PressSeconds),Release=1-FMath::Exp(-Step/S.RecoverSeconds);
    TargetDepth.Init(0,Rest.Num()); TargetGradient.Init(FVector::ZeroVector,Rest.Num());
    if (Active) for (const auto& Load:Sources)
    {
        const FVector Center=Transform.TransformPosition(Load.LocalPoint);
        const FVector X=Load.Axis,Y=FVector::CrossProduct(FVector::UpVector,X);
        const FVector GX=X/Load.RadiusX,GY=Y/Load.RadiusY;
        const float EX=FMath::Abs(X.X)*Load.RadiusX+FMath::Abs(Y.X)*Load.RadiusY;
        const float EY=FMath::Abs(X.Y)*Load.RadiusX+FMath::Abs(Y.Y)*Load.RadiusY;
        for(int32 CX=FMath::FloorToInt((Center.X-EX)/PressureCellSize);CX<=FMath::FloorToInt((Center.X+EX)/PressureCellSize);++CX)
        for(int32 CY=FMath::FloorToInt((Center.Y-EY)/PressureCellSize);CY<=FMath::FloorToInt((Center.Y+EY)/PressureCellSize);++CY)
        if (const auto* Cell=PressureCells.Find(FIntPoint(CX,CY))) for(int32 I:*Cell)
        {
            const FVector D=PressureWorldVertices[I]-Center;
            const float DX=FVector::DotProduct(D,GX),DY=FVector::DotProduct(D,GY);
            const float R2=DX*DX+DY*DY; if (R2>=1) continue;
            const float K=1-R2,Kernel=FMath::Pow(K,S.FalloffPower-1);
            TargetDepth[I]+=Load.Depth*Kernel*K;
            TargetGradient[I]+=-2*S.FalloffPower*Load.Depth*Kernel*(GX*DX+GY*DY);
        }
    }
    for (int32 I=0;I<Rest.Num();++I)
    {
        const float Sum=TargetDepth[I]; FVector Gradient=TargetGradient[I];
        // Smooth saturation bounds overlapping dents and keeps their normals continuous.
        const float Denom=S.MaxDepth+Sum;
        Gradient*=FMath::Square(S.MaxDepth/Denom);
        const float Depth=S.MaxDepth*Sum/Denom/ZScale;
        const float U=(Rest[I].Z-RestBounds.Min.Z)/Size.Z;
        const float Top=FMath::SmoothStep(.15f,.75f,U),T=FMath::Clamp((U-.15f)/.6f,0.f,1.f);
        const float TopSlope=6*T*(1-T)/(.6f*Size.Z);
        const FVector G=Transform.InverseTransformVectorNoScale(Gradient)*Scale/ZScale*Top+FVector(0,0,Depth*TopSlope);
        const float Target=Depth*Top,Alpha=Target>IndentDepth[I]?Press:Release;
        if (Active && Target>=IndentDepth[I]-.001f && Target>.001f) PressureHold[I]=S.TrailHoldSeconds;
        else PressureHold[I]=FMath::Max(0.f,PressureHold[I]-Dt);
        if (Target>=IndentDepth[I] || PressureHold[I]<=0 || !S.bEnabled)
        {
            IndentDepth[I]=FMath::Lerp(IndentDepth[I],Target,Alpha);
            IndentGradient[I]=FMath::Lerp(IndentGradient[I],G,Alpha);
        }
        if (IndentDepth[I]>S.MaxDepth/ZScale)
        { IndentGradient[I]*=(S.MaxDepth/ZScale)/IndentDepth[I]; IndentDepth[I]=S.MaxDepth/ZScale; }
        if (Target==0 && IndentDepth[I]<.001f) { IndentDepth[I]=0; IndentGradient[I]=FVector::ZeroVector; PressureHold[I]=0; }
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
    PressureEpoch=PressureFrame.Epoch; IndentDepth.Init(0,Rest.Num()); IndentGradient.Init(FVector::ZeroVector,Rest.Num()); PressureHold.Init(0,Rest.Num());
}
void AMCTongue::ResetPressure()
{
    if (!HasAuthority()) return;
    CurrentLoads.Empty(); PressureFrame.Sources.Empty(); LoadHistory.Empty(); ++PressureFrame.Epoch;
    PressureFrame.UpdatedAt=ServerTime(); OnRep_PressureFrame(); ForceNetUpdate();
}
