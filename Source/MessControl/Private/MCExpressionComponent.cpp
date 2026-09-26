#include "MCExpressionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGripComponent.h"
#include "MCFoodActor.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

float FMCEmoteEntry::Length() const { return Animation?Animation->GetPlayLength():FMath::Clamp(Duration,.5f,10.f); }
UMCExpressionComponent::UMCExpressionComponent()
{
    SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick=true;
    Profile=TSoftObjectPtr<UMCEmoteLibrary>(FSoftObjectPath(TEXT("/Game/Data/DA_Emotes.DA_Emotes")));
}
void UMCExpressionComponent::BeginPlay()
{
    Super::BeginPlay(); Tooth=CastChecked<AMCToothCharacter>(GetOwner()); Library=Profile.LoadSynchronous();
    Tooth->GetMesh()->AddTickPrerequisiteComponent(this);
}
double UMCExpressionComponent::Now() const
{
    const auto* GS=GetWorld()->GetGameState(); return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}
const FMCEmoteEntry* UMCExpressionComponent::ActiveEntry() const
{
    return Library?Library->Entries.FindByPredicate([&](const FMCEmoteEntry& E){return E.Id==State.Id;}):nullptr;
}
bool UMCExpressionComponent::CanPlay(const FMCEmoteEntry& Entry) const
{
    if (!Tooth || !Tooth->ToothPhysics->CanAct() || !Tooth->Status->IsAlive()) return false;
    if (!Tooth->Status->State.bCareReaction && Now()-Tooth->Status->State.ReactionAt<.65) return false;
    return !Entry.Animation || (!Tooth->bHandling && !Tooth->bBrushing && !Tooth->HeldFood && !Tooth->ClingTooth
        && Tooth->GetVelocity().Size2D()<25 && Tooth->GetCharacterMovement()->IsMovingOnGround());
}
void UMCExpressionComponent::ServerPlayEmote_Implementation(FName Id)
{
    if (!Library || Now()<NextEmoteAt) return;
    const auto* Entry=Library->Entries.FindByPredicate([&](const FMCEmoteEntry& E){return E.Id==Id;});
    if (!Entry || !CanPlay(*Entry) || (!State.Id.IsNone() && EmoteAlpha()>.01f)) return;
    State.Id=Id; State.StartedAt=Now(); State.StoppedAt=-1; ++State.Serial; NextEmoteAt=Now()+.4;
    Tooth->ForceNetUpdate();
}
float UMCExpressionComponent::EmoteAlpha() const
{
    const auto* Entry=ActiveEntry(); if (!Entry) return 0;
    const double End=State.StoppedAt>=0?FMath::Min(State.StoppedAt,State.StartedAt+Entry->Length()):State.StartedAt+Entry->Length();
    const float In=FMath::Clamp(float((Now()-State.StartedAt)/.18),0.f,1.f);
    const float Out=FMath::Clamp(float((End+.22-Now())/.22),0.f,1.f);
    return FMath::SmoothStep(0.f,1.f,FMath::Min(In,Out));
}
float UMCExpressionComponent::BodyAlpha() const
{
    const auto* E=ActiveEntry(); return E && E->Animation?EmoteAlpha():0;
}
void UMCExpressionComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Dt,Type,TickFunction);
    if (Now()-VoiceAt>.25) { Voice=0; VoiceViseme=MCViseme::Rest; }
    if (Tooth && Tooth->HasAuthority() && State.StoppedAt<0)
        if (const auto* Entry=ActiveEntry(); Entry && !CanPlay(*Entry)) { State.StoppedAt=Now(); Tooth->ForceNetUpdate(); }
}
void UMCExpressionComponent::SetSpeechInput(float Envelope,MCViseme Viseme)
{
    Voice=FMath::IsFinite(Envelope)?FMath::Clamp(Envelope,0.f,1.f):0;
    VoiceViseme=Viseme; VoiceAt=Now();
}
void UMCExpressionComponent::BuildBodyPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref) const
{
    const auto* Entry=ActiveEntry(); const float Alpha=BodyAlpha();
    if (!Entry || Alpha<.001f || !Tooth->ToothPhysics->CanAct()) return;
    const auto* Skeleton=Entry->Animation->GetSkeleton(); if (!Skeleton) return;
    const double SampleNow=State.StoppedAt>=0?FMath::Min(Now(),State.StoppedAt):Now();
    const double Time=FMath::Clamp(SampleNow-State.StartedAt,0.,double(Entry->Length()));
    FAnimExtractContext Context(Time,false);
    for (int32 I=0;I<Pose.Num();++I)
    {
        const FName Name=Ref.GetBoneName(I); const FString Bone=Name.ToString();
        // Facial controls remain available to gaze, blinking, reactions and speech.
        if (Bone.StartsWith(TEXT("c_"))) continue;
        const int32 Index=Skeleton->GetReferenceSkeleton().FindBoneIndex(Name); if (Index<0) continue;
        FTransform Sample;
        Entry->Animation->GetBoneTransform(Sample,FSkeletonPoseBoneIndex(Index),Context,false);
        FTransform Blended; Blended.Blend(Pose[I],Sample,Alpha); Pose[I]=Blended;
    }
}
void UMCExpressionComponent::BuildFacePose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref,float Dt)
{
    if (!Tooth) return;
    const auto& S=Tooth->Status->State;
    const float Pain=!S.bCareReaction?FMath::Clamp(1.f-float(Now()-S.ReactionAt)/1.1f,0.f,1.f):0;
    float Strength=1; CurrentEmotion=EMCEmotion::Neutral;
    if (const auto* Entry=ActiveEntry(); Entry && EmoteAlpha()>.01f) { CurrentEmotion=Entry->Emotion; Strength=EmoteAlpha(); }
    else if (Tooth->HeldFood && !Tooth->Grip->InputDirection().IsNearlyZero()) { CurrentEmotion=EMCEmotion::Effort; Strength=FMath::Clamp(Tooth->HeldFood->Settings.Mass/28.f,.25f,1.f); }
    else if (!Tooth->ToothPhysics->CanAct() || Tooth->GetCharacterMovement()->IsFalling()) { CurrentEmotion=EMCEmotion::Surprise; Strength=.75f; }
    else if (S.Health<S.MaxHealth*.35f) { CurrentEmotion=EMCEmotion::Sad; Strength=.6f; }
    if (Pain>.01f) { CurrentEmotion=EMCEmotion::Pain; Strength=Pain; }
    float J=0,Sml=0,B=0,Tilt=0,Squ=0,Rnd=0;
    switch (CurrentEmotion)
    {
    case EMCEmotion::Happy: Sml=1; B=.25f; Squ=.18f; break;
    case EMCEmotion::Angry: Sml=-.25f; Tilt=1; Squ=.25f; break;
    case EMCEmotion::Sad: Sml=-.65f; Tilt=-.65f; B=-.15f; break;
    case EMCEmotion::Surprise: J=.65f; B=1; Rnd=.5f; break;
    case EMCEmotion::Pain: J=.5f; Tilt=.8f; Squ=.8f; Sml=-.4f; break;
    case EMCEmotion::Effort: Tilt=.55f; Squ=.4f; Sml=-.2f; break;
    default: break;
    }
    J*=Strength; Sml*=Strength; B*=Strength; Tilt*=Strength; Squ*=Strength; Rnd*=Strength;
    if (Pain<.1f && Voice>0)
    {
        const float Open=VoiceViseme==MCViseme::Closed || VoiceViseme==MCViseme::Rest?0:VoiceViseme==MCViseme::LipBite?.12f:Voice;
        J=FMath::Max(J,Open*.75f);
        if (VoiceViseme==MCViseme::Wide) Sml=FMath::Max(Sml,Voice*.65f);
        if (VoiceViseme==MCViseme::Round) Rnd=FMath::Max(Rnd,Voice);
    }
    auto Smooth=[&](float& V,float Target){V=FMath::FInterpTo(V,Target,Dt,14.f);};
    Smooth(Jaw,J); Smooth(Smile,Sml); Smooth(Brows,B); Smooth(BrowTilt,Tilt); Smooth(EyeSquint,Squ); Smooth(Round,Rnd);
    Smooth(LipClosure,Pain<.1f && Voice>0 && VoiceViseme==MCViseme::Closed?Voice:0.f);
    TArray<FTransform> RestCS; RestCS.SetNum(Pose.Num());
    for (int32 I=0;I<Pose.Num();++I) RestCS[I]=Ref.GetParentIndex(I)>=0?Ref.GetRefBonePose()[I]*RestCS[Ref.GetParentIndex(I)]:Ref.GetRefBonePose()[I];
    auto Offset=[&](FName Bone,FVector Delta)
    {
        const int32 I=Ref.FindBoneIndex(Bone); if (I<0) return;
        const int32 Parent=Ref.GetParentIndex(I);
        Pose[I].AddToTranslation(Parent>=0?RestCS[Parent].InverseTransformVectorNoScale(Delta):Delta);
    };
    // Mesh-space deltas in cm. Separate lip controls avoid pulling the enamel shell.
    Offset(TEXT("c_jawbone_x"),FVector(0,0,-2.8f*Jaw));
    Offset(TEXT("c_lips_top_x"),FVector(0,0,-1.8f*FMath::Max(0.f,Smile)-3.5f*LipClosure));
    Offset(TEXT("c_lips_bot_x"),FVector(0,0,1.2f*FMath::Max(0.f,Smile)+3.5f*LipClosure));
    for (const FString Side:{FString(TEXT("l")),FString(TEXT("r"))})
    {
        const float Sign=Side==TEXT("l")?1.f:-1.f;
        Offset(FName(*(TEXT("c_lips_smile_")+Side)),FVector(Sign*(Smile*3.f-Round*1.6f),0,Smile*3.5f));
        Offset(FName(*(TEXT("c_eyebrow_full_")+Side)),FVector(0,0,Brows*1.7f));
        Offset(FName(*(TEXT("c_eyebrow_01_")+Side)),FVector(0,0,-BrowTilt*2.5f));
        Offset(FName(*(TEXT("c_eyebrow_03_")+Side)),FVector(0,0,BrowTilt*1.2f));
    }
}
void UMCExpressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCExpressionComponent,State);
}
