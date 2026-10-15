#include "MCPlayerCameraComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCRewardChest.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"

namespace MCPlayerCameraPrivate
{
    constexpr int32 RevealFocusIndex=20,RevealSettingsIndex=24;
    bool CanSpectatorRevealWall(const UMeshComponent* Wall)
    {
        if(!Wall || !Wall->IsRegistered() || !Wall->IsQueryCollisionEnabled() || Wall->GetNumMaterials()==0) return false;
        for(int32 Slot=0;Slot<Wall->GetNumMaterials();++Slot) {
            const auto* Material=Wall->GetMaterial(Slot); float Enabled=0;
            if(!Material || !Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("CameraRevealEnabled")),Enabled) || Enabled<.5f) return false;
        }
        return Wall->GetCustomPrimitiveDataIndexForVectorParameter(TEXT("CameraRevealFocus"))==RevealFocusIndex
            && Wall->GetCustomPrimitiveDataIndexForVectorParameter(TEXT("CameraRevealSettings"))==RevealSettingsIndex;
    }
    void RestoreSpectatorWall(UMeshComponent* Wall,const FMCSpectatorCameraViewState::FRevealedWall& State)
    {
        if(!Wall) return;
        Wall->SetCustomPrimitiveDataFloat(RevealSettingsIndex,0);
        Wall->SetCollisionResponseToChannel(ECC_Camera,State.CameraResponse);
    }
    void PrepareSpectatorWalls(const AMCToothCharacter* Target,FMCSpectatorCameraViewState& State)
    {
        for(auto It=State.RevealedWalls.CreateIterator();It;++It) {
            auto* Wall=It.Key().Get();
            if(Wall && Target->bCameraWallReveal && CanSpectatorRevealWall(Wall)) continue;
            RestoreSpectatorWall(Wall,It.Value()); It.RemoveCurrent();
        }
        if(!Target->bCameraWallReveal) return;
        const double Now=Target->GetWorld()->GetTimeSeconds();
        if(Now<State.NextWallScan) return;
        State.NextWallScan=Now+.5;
        for(TActorIterator<AActor> It(Target->GetWorld());It;++It) {
            if(*It==Target) continue;
            TInlineComponentArray<UMeshComponent*> Components; It->GetComponents(Components);
            for(auto* Wall:Components) {
                if(State.RevealedWalls.Contains(Wall) || Wall->GetCollisionResponseToChannel(ECC_Camera)!=ECR_Block
                    || !CanSpectatorRevealWall(Wall)) continue;
                auto& Revealed=State.RevealedWalls.Add(Wall); Revealed.CameraResponse=Wall->GetCollisionResponseToChannel(ECC_Camera);
                // The spectator owns the local camera-channel override and restores only its own walls.
                Wall->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
            }
        }
    }
    void PresentSpectatorWalls(const AMCToothCharacter* Target,float Dt,const FVector& Eye,const FVector& Focus,
        FMCSpectatorCameraViewState& State)
    {
        const float Blend=1.f-FMath::Exp(-10.f*FMath::Max(0.f,Dt));
        for(auto& Entry:State.RevealedWalls) if(auto* Wall=Entry.Key.Get()) {
            FHitResult Hit;
            const bool Occluded=Wall->SweepComponent(Hit,Eye,Focus,FQuat::Identity,FCollisionShape::MakeSphere(45),true);
            auto& Revealed=Entry.Value; Revealed.Amount=FMath::Lerp(Revealed.Amount,Occluded?1.f:0.f,Blend);
            if(Revealed.Amount<.001f) Revealed.Amount=0;
            Wall->SetCustomPrimitiveDataVector4(RevealFocusIndex,FVector4(Focus,0));
            Wall->SetCustomPrimitiveDataVector4(RevealSettingsIndex,FVector4(Revealed.Amount,
                FMath::Clamp(Target->CameraWallRevealRadius,60.f,500.f),FMath::Clamp(Target->CameraWallRevealFeather,.02f,.8f),0));
        }
    }
}

namespace {
    constexpr float ImpactDuration=.28f;
    constexpr float GroundImpactDuration=.7f;
    float ImpactEnvelope(double Age,float Duration=ImpactDuration) {
        const float Remaining=1.f-FMath::Clamp(float(Age)/Duration,0.f,1.f);
        return Remaining*Remaining;
    }
}

UMCPlayerCameraComponent::UMCPlayerCameraComponent()
{
    PostProcessBlendWeight=1.f;
    PostProcessSettings.bOverride_DepthOfFieldEnabled=true;
    PostProcessSettings.DepthOfFieldEnabled=true;
    PostProcessSettings.bOverride_DepthOfFieldFstop=true;
    PostProcessSettings.DepthOfFieldFstop=1.8f;
    PostProcessSettings.bOverride_DepthOfFieldSensorWidth=true;
    PostProcessSettings.DepthOfFieldSensorWidth=36.f;
    PostProcessSettings.bOverride_DepthOfFieldFocalDistance=true;
    PostProcessSettings.DepthOfFieldFocalDistance=900.f;
}

void UMCPlayerCameraComponent::GetSpectatorView(const AMCToothCharacter* Target,const AActor* ViewerPawn,
    FRotator Orbit,float Distance,float DeltaTime,FMCSpectatorCameraViewState& State,FMinimalViewInfo& View)
{
    if(!IsValid(Target) || !Target->GetWorld()) return;
    const FVector Position=Target->GetActorLocation();
    const bool Reset=!State.bInitialized || FVector::DistSquared(Position,State.Focus)>FMath::Square(1400.f);
    const float Dt=FMath::Max(0.f,DeltaTime);
    const float Speed=FMath::Clamp(Target->FollowSpeed,1.f,20.f);
    const float Blend=1.f-FMath::Exp(-Speed*Dt);
    State.Focus=Reset?Position:FMath::Lerp(State.Focus,Position,Blend);
    Orbit.Pitch=FMath::Clamp(Orbit.Pitch,-85.,85.); Orbit.Roll=0;
    State.Rotation=Reset?Orbit:FMath::RInterpTo(State.Rotation,Orbit,Dt,Speed);
    Distance=FMath::Clamp(Distance,250.f,1600.f);
    State.Distance=Reset?Distance:FMath::Lerp(State.Distance,Distance,Blend);
    // Match the ordinary free orbit: upward looking never swings the eye below the floor.
    const FVector EyeAxis=FRotator(FMath::Min(State.Rotation.Pitch,0.),State.Rotation.Yaw,0).Vector();
    const FVector Wanted=State.Focus+FVector(0,0,FMath::Clamp(Target->CameraOrbitHeight,30.f,250.f))-EyeAxis*State.Distance;
    const FVector Origin=Position+FVector(0,0,30);
    MCPlayerCameraPrivate::PrepareSpectatorWalls(Target,State);
    FVector Safe=Wanted;
    const auto* Boom=Target->CameraBoom.Get();
    if(!Boom || Boom->bDoCollisionTest) {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCSpectatorCamera),false,Target);
        if(ViewerPawn) Query.AddIgnoredActor(ViewerPawn);
        if(IsValid(Target->MimicCaptor)) Query.AddIgnoredActor(Target->MimicCaptor);
        FHitResult Hit;
        const float Radius=Boom?FMath::Max(1.f,Boom->ProbeSize):24.f;
        const ECollisionChannel Channel=Boom?Boom->ProbeChannel.GetValue():ECC_Camera;
        if(Target->GetWorld()->SweepSingleByChannel(Hit,Origin,Wanted,FQuat::Identity,Channel,
            FCollisionShape::MakeSphere(Radius),Query)) Safe=Hit.Location;
    }
    const float Limit=float(FVector::Distance(Origin,Safe));
    if(Reset || State.SafeArmLength<0.f) State.SafeArmLength=Limit;
    // Retract immediately; only outward travel is smoothed, as on the ordinary camera arm.
    State.SafeArmLength=FMath::Min(Limit,FMath::Lerp(State.SafeArmLength,Limit,1.f-FMath::Exp(-12.f*Dt)));
    View=FMinimalViewInfo();
    View.Location=Origin+(Safe-Origin).GetSafeNormal()*State.SafeArmLength;
    // An aperture bypasses the view ray, but the eye's sphere must remain outside the wall volume.
    const float EyeRadius=(Boom?FMath::Max(1.f,Boom->ProbeSize):24.f)+2.f;
    for(const auto& Entry:State.RevealedWalls) if(auto* Wall=Entry.Key.Get()) {
        if(!Wall->Bounds.GetBox().ExpandBy(EyeRadius).IsInsideOrOn(View.Location)) continue;
        FHitResult Touch,Contact;
        if(!Wall->SweepComponent(Touch,View.Location,View.Location+FVector(0,0,.1),FQuat::Identity,
            FCollisionShape::MakeSphere(EyeRadius),true)) continue;
        if(Wall->SweepComponent(Contact,Origin,View.Location,FQuat::Identity,FCollisionShape::MakeSphere(EyeRadius),true)) {
            State.SafeArmLength=FMath::Min(State.SafeArmLength,FMath::Max(0.f,float(FVector::Distance(Origin,Contact.Location))-2.f));
            View.Location=Origin+(View.Location-Origin).GetSafeNormal()*State.SafeArmLength;
        }
    }
    View.Rotation=State.Rotation;
    View.FOV=FMath::Clamp(Target->FollowFOV,45.f,95.f);
    View.AspectRatio=16.f/9.f;
    View.AspectRatioAxisConstraint=AspectRatio_MaintainYFOV;
    if(const auto* Camera=Target->Camera.Get()) {
        View.PostProcessSettings=Camera->PostProcessSettings;
        View.PostProcessBlendWeight=Camera->PostProcessBlendWeight;
        if(View.PostProcessSettings.bOverride_DepthOfFieldFocalDistance)
            View.PostProcessSettings.DepthOfFieldFocalDistance=FMath::Max(10.f,float(FVector::DotProduct(
                Target->GetCameraFocusLocation()-View.Location,View.Rotation.Vector())));
    }
    State.bInitialized=true;
    MCPlayerCameraPrivate::PresentSpectatorWalls(Target,Dt,View.Location,Origin,State);
}

void UMCPlayerCameraComponent::ClearSpectatorView(FMCSpectatorCameraViewState& State)
{
    for(const auto& Entry:State.RevealedWalls)
        MCPlayerCameraPrivate::RestoreSpectatorWall(Entry.Key.Get(),Entry.Value);
    State=FMCSpectatorCameraViewState();
}

void UMCPlayerCameraComponent::AddImpact(float ImpactSpeed, const FVector& WorldDirection)
{
    if(!GetWorld() || !FMath::IsFinite(ImpactSpeed) || ImpactSpeed<=120.f || WorldDirection.ContainsNaN()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    const float Speed=FMath::Min(ImpactSpeed,1000.f);
    // Relative kinetic energy per unit mass; cap large hits and overlapping contacts.
    const float Strength=(Speed*Speed-120.f*120.f)/(1000.f*1000.f-120.f*120.f);
    const float Existing=ImpactStrength*ImpactEnvelope(Now-ImpactStartedAt);
    if(Now-ImpactStartedAt<.1 && Strength<=Existing) return;
    ImpactStrength=FMath::Max(Existing,Strength);
    ImpactDirection=WorldDirection.GetSafeNormal();
    ImpactStartedAt=Now;
}

void UMCPlayerCameraComponent::AddGroundImpact(float Strength,const FVector& Source)
{
    if(!GetWorld() || !FMath::IsFinite(Strength) || Strength<=0.f || Source.ContainsNaN()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    const float Existing=GroundImpactStrength*ImpactEnvelope(Now-GroundImpactStartedAt,GroundImpactDuration);
    Strength=FMath::Clamp(Strength,0.f,1.f);
    if(Now-GroundImpactStartedAt<.08 && Strength<=Existing) return;
    GroundImpactStrength=FMath::Max(Existing,Strength);
    GroundImpactSource=Source;GroundImpactStartedAt=Now;
}

void UMCPlayerCameraComponent::AddCombatConfirm(float Strength)
{
    if(!GetWorld() || !FMath::IsFinite(Strength) || Strength<=0.f) return;
    if(GetOwner() && GetOwner()->ActorHasTag(TEXT("MC.LegacyCombatFeedback"))) return;
    const double Now=GetWorld()->GetTimeSeconds();
    // Several contacts in a buffer pulse produce one bounded camera accent.
    Strength=FMath::Clamp(Strength,0.f,1.f);
    if(Now-CombatConfirmAt<.08) {CombatConfirmStrength=FMath::Max(CombatConfirmStrength,Strength);return;}
    CombatConfirmAt=Now;CombatConfirmStrength=Strength;
}

void UMCPlayerCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
    Super::GetCameraView(DeltaTime,DesiredView);
    const double Age=GetWorld()?GetWorld()->GetTimeSeconds()-ImpactStartedAt:ImpactDuration;
    if(ImpactStrength>0.f && Age>=0. && Age<ImpactDuration) {
        const float Amount=ImpactStrength*ImpactEnvelope(Age)*FMath::Clamp(ImpactShakeScale,0.f,2.f);
        const float Wave=FMath::Cos(float(Age)*2.f*PI*17.f);
        const float CrossWave=FMath::Sin(float(Age)*2.f*PI*13.f);
        const FVector Direction=DesiredView.Rotation.UnrotateVector(ImpactDirection);
        const float Side=Direction.Y<0.f?-1.f:1.f;
        // Angular feedback only; keep the eye position, lens and orbit input unchanged.
        DesiredView.Rotation+=FRotator(-.55f*Wave,.35f*CrossWave*Side,.16f*Wave*Side)*Amount;
    }
    const double GroundAge=GetWorld()?GetWorld()->GetTimeSeconds()-GroundImpactStartedAt:GroundImpactDuration;
    if(GroundImpactStrength>0.f && GroundAge>=0. && GroundAge<GroundImpactDuration) {
        const float Amount=GroundImpactStrength*ImpactEnvelope(GroundAge,GroundImpactDuration)*FMath::Clamp(ImpactShakeScale,0.f,2.f);
        const float Wave=FMath::Cos(float(GroundAge)*2.f*PI*7.f);
        const float CrossWave=FMath::Sin(float(GroundAge)*2.f*PI*9.f);
        const FVector Direction=DesiredView.Rotation.UnrotateVector(GroundImpactSource-GetComponentLocation());
        const float Side=Direction.Y<0.f?-1.f:1.f;
        DesiredView.Rotation+=FRotator(-.9f*Wave,.6f*CrossWave*Side,.35f*Wave*Side)*Amount;
    }
    const auto* Hero=Cast<AMCToothCharacter>(GetOwner());
    if(Hero && !Hero->IsFreezingToDeath() && !Hero->ActorHasTag(TEXT("MC.LegacyCombatFeedback")) && Hero->IsLocallyControlled() && Hero->IsPlayerControlled() && Hero->Status) {
        const float FeedbackScale=FMath::Clamp(ImpactShakeScale,0.f,2.f);
        const double Time=GetWorld()->GetTimeSeconds();
        const double ConfirmAge=Time-CombatConfirmAt;
        if(ConfirmAge>=0. && ConfirmAge<.13) {
            const float Punch=CombatConfirmStrength*ImpactEnvelope(ConfirmAge,.13f)*FeedbackScale;
            const float Wave=FMath::Sin(float(ConfirmAge)*2*PI*21.f);
            DesiredView.Rotation+=FRotator(-.62f,.22f*Wave,.12f*Wave)*Punch;
            DesiredView.FOV-=.8f*Punch;
        }
        const auto& Status=Hero->Status->State;
        if(Status.DamageAt>PresentedDamageAt) {
            const float Loss=PreviousHealth>=0.f?FMath::Max(0.f,PreviousHealth-Status.Health):Status.MaxHealth*.1f;
            DamageStrength=FMath::Clamp(.3f+Loss/FMath::Max(1.f,Status.MaxHealth)*2.f,.3f,1.f);
            PresentedDamageAt=Status.DamageAt;
        }
        PreviousHealth=Status.Health;
        const auto* GS=GetWorld()->GetGameState();
        const double DamageAge=(GS?GS->GetServerWorldTimeSeconds():Time)-Status.DamageAt;
        if(DamageAge>=0. && DamageAge<.32) {
            const float Pulse=DamageStrength*ImpactEnvelope(DamageAge,.32f);
            const float Kick=Pulse*FeedbackScale;
            DesiredView.Rotation+=FRotator(-1.15f*FMath::Cos(float(DamageAge)*PI*9.f),0,
                .55f*FMath::Sin(float(DamageAge)*PI*11.f))*Kick;
            DesiredView.FOV+=1.2f*Kick;
            // Change only this frame's view; the authored post-process settings restore automatically.
            auto& PP=DesiredView.PostProcessSettings;
            PP.bOverride_VignetteIntensity=true;
            PP.VignetteIntensity=FMath::Clamp(PP.VignetteIntensity+.24f*Pulse,0.f,1.f);
            PP.bOverride_SceneColorTint=true;
            PP.SceneColorTint*=FMath::Lerp(FLinearColor::White,FLinearColor(1,.72f,.64f),Pulse*.32f);
        }
    }
    if (!Hero || !bAutoFocusPlayer || PostProcessBlendWeight<=0.f || !PostProcessSettings.bOverride_DepthOfFieldFocalDistance) return;
    const FVector Focus=Hero->GetCameraFocusLocation();
    // Use the final view after the spring arm's collision correction, and focus
    // on the avatar's depth plane even when the follow camera frames it off-center.
    const float Distance=FMath::Max(10.f,float(FVector::DotProduct(Focus-DesiredView.Location,DesiredView.Rotation.Vector())));
    PostProcessSettings.DepthOfFieldFocalDistance=Distance;
    DesiredView.PostProcessSettings.DepthOfFieldFocalDistance=Distance;
}
