#include "MCGazeComponent.h"
#include "MCExpressionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCFoodActor.h"
#include "MCCoffeeFlood.h"
#include "MCGameState.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Misc/ConfigCacheIni.h"

void FMCGazeSettings::Sanitize()
{
    auto C=[](float V,float D,float Min,float Max){return FMath::Clamp(FMath::IsFinite(V)?V:D,Min,Max);};
    Radius=C(Radius,850,100,2000); ViewHalfAngle=C(ViewHalfAngle,80,20,100);
    ScanInterval=C(ScanInterval,.15f,.08f,1); HoldSeconds=C(HoldSeconds,1.2f,.3f,5);
    YawLimit=C(YawLimit,35,0,45); PitchLimit=C(PitchLimit,25,0,35);
    TurnSpeed=C(TurnSpeed,18,3,40); ReactionDelay=C(ReactionDelay,.12f,0,.5f);
    BlinkMin=C(BlinkMin,2.8f,1,15); BlinkMax=C(BlinkMax,5.2f,BlinkMin,20); BlinkSeconds=C(BlinkSeconds,.22f,.12f,.6f);
    PupilRest=C(PupilRest,1.f,.6f,1.8f); PupilDanger=C(PupilDanger,1.75f,PupilRest,1.8f);
    PupilPain=C(PupilPain,1.5f,PupilRest,1.8f); PupilFocus=C(PupilFocus,.8f,.6f,PupilRest);
    PupilReactSpeed=C(PupilReactSpeed,12.f,2.f,30.f); PupilRecoverSpeed=C(PupilRecoverSpeed,2.4f,.5f,10.f);
}
UMCGazeComponent::UMCGazeComponent()
{
    PrimaryComponentTick.bCanEverTick=true; SetIsReplicatedByDefault(true);
    Profile=TSoftObjectPtr<UMCGazeProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_Gaze.DA_Gaze")));
}
void UMCGazeComponent::BeginPlay()
{
    Super::BeginPlay(); Tooth=Cast<AMCToothCharacter>(GetOwner()); if (!Tooth) return;
    Tooth->GetMesh()->AddTickPrerequisiteComponent(this);
    if (GetOwner()->HasAuthority())
    {
        if (auto* P=Profile.LoadSynchronous()) Settings=P->Settings;
        Settings.Sanitize(); NextBlink=ServerTime()+FMath::FRandRange(1.f,Settings.BlinkMax);
    }
}
float UMCGazeComponent::ServerTime() const
{
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    return State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
FVector UMCGazeComponent::EyePosition() const
{
    const auto* Mesh=Tooth->GetMesh();
    const FName L=Tooth->RigBone(TEXT("eye_l")),R=Tooth->RigBone(TEXT("eye_r"));
    if (Mesh->GetBoneIndex(L)!=INDEX_NONE && Mesh->GetBoneIndex(R)!=INDEX_NONE)
        return (Mesh->GetBoneLocation(L)+Mesh->GetBoneLocation(R))*.5;
    return Tooth->GetActorLocation()+FVector(0,0,25);
}
FVector UMCGazeComponent::TargetPoint() const
{
    return IsValid(Target.Actor)?Target.Actor->GetActorTransform().TransformPosition(Target.Offset):FVector(Target.Point);
}
bool UMCGazeComponent::CanSee(AActor* Actor,FVector Point) const
{
    const FVector From=EyePosition(),Delta=Point-From;
    if (Point.ContainsNaN() || Delta.SizeSquared()>FMath::Square(Settings.Radius) || Delta.SizeSquared()<1) return false;
    if (FVector::DotProduct(Tooth->GetActorForwardVector(),Delta.GetSafeNormal2D())<FMath::Cos(FMath::DegreesToRadians(Settings.ViewHalfAngle))) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCGaze),false,Tooth);
    return !GetWorld()->LineTraceSingleByChannel(Hit,From,Point,ECC_Visibility,Params) || Hit.GetActor()==Actor;
}
void UMCGazeComponent::SetTarget(AActor* Actor,FVector Point,EMCGazeInterest Interest)
{
    if (Target.Actor==Actor && Target.Interest==Interest && (Actor || FVector::DistSquared(Target.Point,Point)<100)) return;
    Target.Actor=Actor; Target.Point=Point;
    Target.Offset=Actor?Actor->GetActorTransform().InverseTransformPosition(Point):FVector::ZeroVector;
    Target.Interest=Interest; Target.ChangedAt=ServerTime(); ++Target.Serial;
    HoldUntil=ServerTime()+Settings.HoldSeconds; Tooth->ForceNetUpdate();
}
bool UMCGazeComponent::NoticePoint(FVector Point,float Seconds)
{
    if (!Tooth || !Tooth->HasAuthority() || Point.ContainsNaN() || !FMath::IsFinite(Seconds) || !Tooth->Status->IsAlive()) return false;
    if (FVector::DistSquared(Point,EyePosition())>FMath::Square(Settings.Radius)) return false;
    SetTarget(nullptr,Point,EMCGazeInterest::Danger); NoticeUntil=ServerTime()+FMath::Clamp(Seconds,.2f,3.f); return true;
}
void UMCGazeComponent::SelectTarget()
{
    if (!Tooth->Status->IsAlive()) { SetTarget(nullptr,FVector::ZeroVector,EMCGazeInterest::None); return; }
    if (ServerTime()<NoticeUntil) return;
    AActor* Best=nullptr; FVector Point=FVector::ZeroVector; EMCGazeInterest Interest=EMCGazeInterest::None; float Score=-1;
    auto Candidate=[&](AActor* Actor,FVector P,EMCGazeInterest Kind)
    {
        if (!IsValid(Actor) || Actor==Tooth || Actor->IsHidden() || !CanSee(Actor,P)) return;
        const float Value=int32(Kind)*100+30*(1-FVector::Distance(EyePosition(),P)/Settings.Radius)+(Actor==Target.Actor?8:0);
        if (Value>Score) { Best=Actor; Point=P; Interest=Kind; Score=Value; }
    };
    for (TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if (It->Status->IsAlive()) Candidate(*It,It->GetActorLocation()+FVector(0,0,20),EMCGazeInterest::Player);
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if (!It->IsDisposed() && It->Phase!=EMCFoodPhase::Equipped)
            Candidate(*It,It->GetActorLocation(),It->Body->GetPhysicsLinearVelocity().Size()>180 || It->Phase==EMCFoodPhase::Falling?EMCGazeInterest::Danger:EMCGazeInterest::Food);
    if (IsValid(Tooth->HeldFood)) Candidate(Tooth->HeldFood,Tooth->HeldFood->GetActorLocation(),EMCGazeInterest::Work);
    if (IsValid(Tooth->CareTarget) && (Tooth->bBrushing || Tooth->bHandling)) Candidate(Tooth->CareTarget,Tooth->CareTarget->GetActorLocation(),EMCGazeInterest::Work);
    for (TActorIterator<AMCCoffeeFlood> It(GetWorld());It;++It)
        if (It->IsActive()) Candidate(*It,It->WaterSettings.Inlet+FVector(0,0,100),EMCGazeInterest::Danger);
    const bool CurrentValid=IsValid(Target.Actor) && !Target.Actor->IsHidden() && CanSee(Target.Actor,TargetPoint());
    if (CurrentValid && Best!=Target.Actor && ServerTime()<HoldUntil && Interest<=Target.Interest) return;
    SetTarget(Best,Point,Interest);
}
void UMCGazeComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,Type,TickFunction); if (!Tooth) return;
    if (Tooth->IsLocallyControlled() && !bPreviewLoaded)
    {
        bPreviewLoaded=true; FString S;
        if (GConfig->GetString(TEXT("MessControl.Gaze"),TEXT("Preview"),S,GGameUserSettingsIni))
        {
            TArray<FString> V; S.ParseIntoArray(V,TEXT(","));
            if (V.Num()==4) SetVisualPreview(FCString::Atof(*V[0]),FCString::Atof(*V[1]),FCString::Atof(*V[2]),FCString::Atof(*V[3]));
        }
    }
    if (Tooth->HasAuthority())
    {
        ScanLeft-=Dt; if (ScanLeft<=0) { ScanLeft=Settings.ScanInterval; SelectTarget(); }
        if (ServerTime()>=NextBlink)
        {
            BlinkStartedAt=ServerTime()+.15; NextBlink=BlinkStartedAt+FMath::FRandRange(Settings.BlinkMin,Settings.BlinkMax); Tooth->ForceNetUpdate();
        }
    }
    const float Age=(ServerTime()-BlinkStartedAt)/Settings.BlinkSeconds;
    Blink=Age>=0 && Age<1?FMath::Square(FMath::Sin(PI*Age)):0;
    if (!Tooth->Status->IsAlive()) Blink=1;
    const auto S=VisualSettings();
    float Goal=S.PupilRest;
    if (Tooth->Status->IsAlive())
    {
        if (Target.Interest==EMCGazeInterest::Work) Goal=S.PupilFocus;
        if (const auto* E=Tooth->Expression.Get(); E && E->EmoteAlpha()>.01f)
        {
            if (const auto* Entry=E->ActiveEntry(); Entry)
            {
                if (Entry->Emotion==EMCEmotion::Angry) Goal=FMath::Lerp(Goal,S.PupilFocus,E->EmoteAlpha());
                if (Entry->Emotion==EMCEmotion::Surprise) Goal=FMath::Lerp(Goal,S.PupilDanger,E->EmoteAlpha());
            }
        }
        const auto& State=Tooth->Status->State;
        const float Pain=!State.bCareReaction?FMath::Clamp(1.f-(ServerTime()-float(State.ReactionAt))/1.1f,0.f,1.f):0;
        if (Pain>.001f) Goal=FMath::Max(Goal,FMath::Lerp(S.PupilRest,S.PupilPain,Pain));
        if (Target.Interest==EMCGazeInterest::Danger || !Tooth->ToothPhysics->CanAct()) Goal=S.PupilDanger;
    }
    // The server already replicates attention, reactions and emotes. Each peer
    // derives the cosmetic pupil response without streaming another float.
    const bool Recovering=FMath::IsNearlyEqual(Goal,S.PupilRest,.01f);
    const float Speed=Recovering?S.PupilRecoverSpeed:S.PupilReactSpeed;
    PupilScale=FMath::Clamp(FMath::Lerp(PupilScale,Goal,1-FMath::Exp(-Speed*FMath::Max(0.f,Dt))),.6f,1.8f);
    auto* Mesh=Tooth->GetMesh();
    if (Mesh->GetSkeletalMeshAsset() && Mesh->GetSkeletalMeshAsset()->FindMorphTarget(TEXT("Pupil_Dilate")))
    {
        Mesh->SetMorphTarget(TEXT("Pupil_Dilate"),FMath::Clamp((PupilScale-1.f)/.8f,0.f,1.f));
        Mesh->SetMorphTarget(TEXT("Pupil_Contract"),FMath::Clamp((1.f-PupilScale)/.4f,0.f,1.f));
    }
}
FMCGazeSettings UMCGazeComponent::VisualSettings() const
{
    auto S=Settings;
    if (bPreview) { S.YawLimit=Preview.X; S.PitchLimit=Preview.Y; S.TurnSpeed=Preview.Z; S.ReactionDelay=Preview.W; }
    S.Sanitize(); return S;
}
void UMCGazeComponent::SetVisualPreview(float Yaw,float Pitch,float Speed,float Delay)
{
    bPreview=true; Preview=FVector4(Yaw,Pitch,Speed,Delay); const auto S=VisualSettings(); Preview=FVector4(S.YawLimit,S.PitchLimit,S.TurnSpeed,S.ReactionDelay);
}
void UMCGazeComponent::SavePreview()
{
    if (!bPreview) return;
    GConfig->SetString(TEXT("MessControl.Gaze"),TEXT("Preview"),*FString::Printf(TEXT("%f,%f,%f,%f"),Preview.X,Preview.Y,Preview.Z,Preview.W),GGameUserSettingsIni); GConfig->Flush(false,GGameUserSettingsIni);
}
void UMCGazeComponent::ResetPreview()
{
    bPreview=false; GConfig->RemoveKey(TEXT("MessControl.Gaze"),TEXT("Preview"),GGameUserSettingsIni); GConfig->Flush(false,GGameUserSettingsIni);
}
void UMCGazeComponent::BuildPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt)
{
    if (!Tooth || Pose.Num()!=Ref.GetNum()) return;
    TArray<FTransform> CS,Rest; CS.SetNum(Pose.Num()); Rest.SetNum(Pose.Num());
    for (int32 I=0;I<Pose.Num();++I)
    {
        const int32 P=Ref.GetParentIndex(I);
        CS[I]=P>=0?Pose[I]*CS[P]:Pose[I]; Rest[I]=P>=0?Ref.GetRefBonePose()[I]*Rest[P]:Ref.GetRefBonePose()[I];
    }
    const int32 Head=Ref.FindBoneIndex(Tooth->RigBone(TEXT("gaze_head")));
    if (Head==INDEX_NONE) return;
    const FQuat HeadDelta=CS[Head].GetRotation()*Rest[Head].GetRotation().Inverse();
    const FQuat Facing=Tooth->StandingMeshTransform().GetRotation().Inverse();
    // On the authority Chaos applies the body's pose after animation evaluation.
    // Aim in that physical head frame, then write a relative facial pose for it.
    const bool Physical=Tooth->ToothPhysics->GetBodyState()==EMCBodyState::Ragdoll;
    const FQuat AimHead=Physical?Tooth->GetMesh()->GetSocketTransform(Ref.GetBoneName(Head),RTS_Component).GetRotation()*Rest[Head].GetRotation().Inverse():HeadDelta;
    const FVector Forward=AimHead.RotateVector(Facing.RotateVector(FVector::ForwardVector));
    const FVector AimRight=AimHead.RotateVector(Facing.RotateVector(FVector::RightVector));
    const FVector AimUp=AimHead.RotateVector(FVector::UpVector);
    const FVector Right=HeadDelta.RotateVector(Facing.RotateVector(FVector::RightVector));
    const FVector Up=HeadDelta.RotateVector(FVector::UpVector);
    const auto S=VisualSettings();
    const FVector Aim=Tooth->GetMesh()->GetComponentTransform().InverseTransformPosition(TargetPoint());
    for (int32 Side=0;Side<2;++Side)
    {
        const int32 Eye=Ref.FindBoneIndex(Tooth->RigBone(Side==0?TEXT("eye_l"):TEXT("eye_r"))); if (Eye==INDEX_NONE) continue;
        FVector2D& Angles=Side==0?LeftAngles:RightAngles;
        FVector2D Goal=FVector2D::ZeroVector;
        if (Target.Interest!=EMCGazeInterest::None && Tooth->Status->IsAlive())
        {
            const FVector EyePoint=Physical?Tooth->GetMesh()->GetSocketTransform(Ref.GetBoneName(Eye),RTS_Component).GetLocation():CS[Eye].GetLocation();
            const FVector D=(Aim-EyePoint).GetSafeNormal();
            Goal.X=FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(D,AimRight),FVector::DotProduct(D,Forward))),-S.YawLimit,S.YawLimit);
            Goal.Y=FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(D,AimUp),FMath::Sqrt(FMath::Square(FVector::DotProduct(D,Forward))+FMath::Square(FVector::DotProduct(D,AimRight))))),-S.PitchLimit,S.PitchLimit);
        }
        const float Delay=Target.Interest==EMCGazeInterest::Danger?S.ReactionDelay*.25f:S.ReactionDelay;
        if (ServerTime()>=Target.ChangedAt+Delay) Angles=FMath::Lerp(Angles,Goal,1-FMath::Exp(-S.TurnSpeed*FMath::Min(Dt,.1f)));
        const FQuat Delta=FQuat(Up,FMath::DegreesToRadians(float(Angles.X)))*FQuat(Right,FMath::DegreesToRadians(float(-Angles.Y)));
        const int32 Parent=Ref.GetParentIndex(Eye);
        // Replace eye rotation from reference, so a replicated ragdoll face is not rotated twice.
        Pose[Eye].SetRotation((CS[Parent].GetRotation().Inverse()*Delta*HeadDelta*Rest[Eye].GetRotation()).GetNormalized());
        for (int32 Part=0;Part<2;++Part)
        {
            const FName Role=Part==0?(Side==0?TEXT("lid_top_l"):TEXT("lid_top_r")):(Side==0?TEXT("lid_bottom_l"):TEXT("lid_bottom_r"));
            const int32 Lid=Ref.FindBoneIndex(Tooth->RigBone(Role));
            if (Lid==INDEX_NONE || !Tooth->Appearance) continue;
            const int32 LidParent=Ref.GetParentIndex(Lid); if (LidParent<0) continue;
            const float Degrees=Part==0?Tooth->Appearance->UpperLidDegrees:Tooth->Appearance->LowerLidDegrees;
            const float Closure=FMath::Max(Blink,Tooth->Expression?Tooth->Expression->Squint():0.f);
            const FQuat Close(Right,FMath::DegreesToRadians(FMath::Clamp(Degrees,-80.f,80.f)*Closure));
            Pose[Lid].SetRotation((CS[LidParent].GetRotation().Inverse()*Close*HeadDelta*Rest[Lid].GetRotation()).GetNormalized());
        }
    }
}
void UMCGazeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCGazeComponent,Settings);
    DOREPLIFETIME(UMCGazeComponent,Target); DOREPLIFETIME(UMCGazeComponent,BlinkStartedAt);
}
