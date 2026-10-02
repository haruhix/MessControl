#include "MCExpressionComponent.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCGripComponent.h"
#include "MCFoodActor.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

float FMCEmoteEntry::Length() const { return Animation && !bHoldFinalPose?Animation->GetPlayLength():FMath::Clamp(Duration,.5f,10.f); }
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
    return !Entry.Animation || Entry.bFaceOnly || (!Tooth->bHandling && !Tooth->bBrushing && !Tooth->HeldFood && !Tooth->ClingTooth
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
    const auto* E=ActiveEntry(); return E && E->Animation && !E->bFaceOnly?EmoteAlpha():0;
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
    VoiceViseme=FMath::IsFinite(Envelope) && uint8(Viseme)<=uint8(MCViseme::D)?Viseme:MCViseme::Rest; VoiceAt=Now();
}
FName UMCExpressionComponent::VisemeShape(MCViseme Viseme)
{
    static const FName Names[]={NAME_None,TEXT("Mouth_A"),TEXT("Mouth_E"),TEXT("Mouth_O"),TEXT("Mouth_MBP"),TEXT("Mouth_FV"),
        TEXT("Mouth_I"),TEXT("Mouth_U"),TEXT("Mouth_L"),TEXT("Mouth_TH"),TEXT("Mouth_CH"),TEXT("Mouth_S"),TEXT("Mouth_D")};
    return uint8(Viseme)<UE_ARRAY_COUNT(Names)?Names[uint8(Viseme)]:NAME_None;
}
TConstArrayView<FName> UMCExpressionComponent::MouthShapes()
{
    static const FName Names[]={TEXT("Mouth_A"),TEXT("Mouth_E"),TEXT("Mouth_O"),TEXT("Mouth_MBP"),TEXT("Mouth_FV"),
        TEXT("Mouth_I"),TEXT("Mouth_U"),TEXT("Mouth_L"),TEXT("Mouth_TH"),TEXT("Mouth_CH"),TEXT("Mouth_S"),TEXT("Mouth_D"),
        TEXT("Mouth_Smile"),TEXT("Mouth_Frown"),TEXT("Mouth_Angry"),TEXT("Mouth_Pain"),TEXT("Mouth_Surprise"),TEXT("Mouth_Effort")};
    return MakeArrayView(Names);
}
bool UMCExpressionComponent::UpdateMouthShapes(float Dt,float EmotionStrength,bool bPain)
{
    auto* Mesh=Tooth->GetMesh();
    if (!Mesh->GetSkeletalMeshAsset() || !Mesh->GetSkeletalMeshAsset()->FindMorphTarget(TEXT("Mouth_A"))) return false;
    static const FName Moods[]={NAME_None,TEXT("Mouth_Smile"),TEXT("Mouth_Angry"),TEXT("Mouth_Frown"),
        TEXT("Mouth_Surprise"),TEXT("Mouth_Pain"),TEXT("Mouth_Effort")};
    const FName Mood=Moods[uint8(CurrentEmotion)];
    const FName Speech=VisemeShape(VoiceViseme);
    const bool Fresh=Now()-VoiceAt<=.25 && !Speech.IsNone() && !bPain;
    // Unvoiced consonants and a closed M/B/P still need articulation at zero volume.
    const bool Consonant=VoiceViseme==MCViseme::Closed || VoiceViseme==MCViseme::LipBite || uint8(VoiceViseme)>=uint8(MCViseme::L);
    const float SpeechWeight=Fresh?(Consonant?1.f:Voice):0.f;
    const float MoodWeight=FMath::Clamp(EmotionStrength,0.f,1.f)*(1-SpeechWeight);
    const float Alpha=1-FMath::Exp(-18.f*FMath::Max(0.f,Dt));
    // These are complete poses, so convex blending preserves the shared mouth contour.
    // A common smoothing factor preserves sum(weights) <= 1 during every transition.
    for (FName Name:MouthShapes())
    {
        const float Target=(Name==Speech?SpeechWeight:0.f)+(Name==Mood?MoodWeight:0.f);
        float& Weight=MouthWeights.FindOrAdd(Name); Weight=FMath::Lerp(Weight,Target,Alpha);
        if (Weight<.0001f) Weight=0;
        if (Mesh->GetSkeletalMeshAsset()->FindMorphTarget(Name)) Mesh->SetMorphTarget(Name,Weight);
    }
    return true;
}
bool UMCExpressionComponent::ApplyMorphBlink(float Closure)
{
    if (!Tooth) return false;
    auto* Mesh=Tooth->GetMesh();
    if (!Mesh->GetSkeletalMeshAsset() || !Mesh->GetSkeletalMeshAsset()->FindMorphTarget(TEXT("Eyes_Blink"))) return false;
    Closure=FMath::Clamp(Closure,0.f,1.f);
    Mesh->SetMorphTarget(TEXT("Eyes_Blink"),Closure);
    // Cancel the expression's eyelid delta as the authored closed pose takes over.
    // This preserves the mouth and avoids adding a second closure to a squint.
    for (FName Name:MouthShapes())
    {
        const FName Correction(*(TEXT("BlinkCancel_")+Name.ToString()));
        if (Mesh->GetSkeletalMeshAsset()->FindMorphTarget(Correction))
        {
            // The artist's effort mouth also closes the eyelids. An external
            // inhale needs an alert grip: remove that lid delta during the
            // brace, while Eyes_Blink above still owns genuine blinks.
            const float Brace=Name==TEXT("Mouth_Effort") && Tooth->IsYawning() && CurrentEmotion==EMCEmotion::Effort
                ?Tooth->YawnPoseAlpha():0.f;
            Mesh->SetMorphTarget(Correction,MouthWeights.FindRef(Name)*FMath::Max(Closure,Brace));
        }
    }
    return true;
}
void UMCExpressionComponent::BuildBodyPose(TArray<FTransform>& Pose,const FReferenceSkeleton& Ref) const
{
    const auto* Entry=ActiveEntry(); const float Alpha=BodyAlpha();
    if (!Entry || Alpha<.001f || !Tooth->ToothPhysics->CanAct()) return;
    const auto* Skeleton=Entry->Animation->GetSkeleton(); if (!Skeleton) return;
    const double SampleNow=State.StoppedAt>=0?FMath::Min(Now(),State.StoppedAt):Now();
    const double Time=FMath::Clamp(SampleNow-State.StartedAt,0.,double(Entry->Animation->GetPlayLength()));
    FAnimExtractContext Context(Time,false);
    for (int32 I=0;I<Pose.Num();++I)
    {
        const FName Name=Ref.GetBoneName(I); const FString Bone=Name.ToString();
        // Facial controls remain available to gaze, blinking, reactions and speech.
        if (Bone.StartsWith(TEXT("c_")) && !Bone.Contains(TEXT("index")) && !Bone.Contains(TEXT("thumb")) && !Bone.Contains(TEXT("middle")) && !Bone.Contains(TEXT("ring")) && !Bone.Contains(TEXT("pinky"))) continue;
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
    if(Tooth->IsYawning()) {
        const float Age=Now()-Tooth->YawnStartedAt,Remaining=Tooth->YawnEndsAt-Now();
        if(Age<.4f) {CurrentEmotion=EMCEmotion::Surprise;Strength=.55f+.25f*FMath::SmoothStep(0.f,.2f,Age);}
        else if(Remaining<.5f) {CurrentEmotion=EMCEmotion::Happy;Strength=.45f*(1-FMath::SmoothStep(0.f,.5f,Remaining));}
        else {CurrentEmotion=EMCEmotion::Effort;Strength=.65f+.35f*Tooth->YawnPoseAlpha();}
    }
    else if (const auto* Entry=ActiveEntry(); Entry && EmoteAlpha()>.01f) { CurrentEmotion=Entry->Emotion; Strength=EmoteAlpha(); }
    else if (Now()-Tooth->TaskFailureAt<2.5) { CurrentEmotion=EMCEmotion::Sad; }
    else if (Now()-Tooth->TaskSuccessAt<2.0) { CurrentEmotion=EMCEmotion::Happy; }
    else if (Tooth->HeldFood && !Tooth->Grip->InputDirection().IsNearlyZero()) { CurrentEmotion=EMCEmotion::Effort; Strength=FMath::Clamp(Tooth->HeldFood->Settings.Mass/28.f,.25f,1.f); }
    else if (!Tooth->ToothPhysics->CanAct() || Tooth->GetCharacterMovement()->IsFalling()) { CurrentEmotion=EMCEmotion::Surprise; Strength=.75f; }
    else if (Tooth->AnimationSwim>.5f && Tooth->AnimationSwimEffort>.1f) { CurrentEmotion=EMCEmotion::Effort; Strength=.3f+.5f*Tooth->AnimationSwimEffort; }
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
    // The inhale is external: the player is concentrating on the grip, not
    // yawning sleepily. Keep an alert, clenched expression during the brace.
    const bool YawnStrain=Tooth->IsYawning() && CurrentEmotion==EMCEmotion::Effort && Pain<.01f;
    if(YawnStrain) {J=0;Sml=-.12f;B=.12f;Tilt=.9f;Squ=.18f;}
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
    const bool MorphMouth=UpdateMouthShapes(Dt,Strength,Pain>.01f);
    // Artist brow poses coexist with the runtime morph mouth, blink and gaze.
    const bool AuthoredFace=Tooth->GetMesh()->GetSkeletalMeshAsset()
        && Tooth->GetMesh()->GetSkeletalMeshAsset()->FindMorphTarget(TEXT("Eyes_Blink"));
    const auto* Artist=ActiveEntry();
    const float ArtistAlpha=!AuthoredFace && Artist && Artist->bFaceOnly && Artist->Animation && Pain<.01f?EmoteAlpha():0;
    if (ArtistAlpha>.001f)
    {
        const auto* Skeleton=Artist->Animation->GetSkeleton();
        FAnimExtractContext Context(FMath::Min(Now()-State.StartedAt,double(Artist->Animation->GetPlayLength())),false);
        for (int32 I=0;I<Pose.Num();++I)
        {
            const FName Name=Ref.GetBoneName(I);
            if (!Name.ToString().StartsWith(TEXT("c_eyebrow")) || !Skeleton) continue;
            const int32 Index=Skeleton->GetReferenceSkeleton().FindBoneIndex(Name); if (Index<0) continue;
            FTransform Sample,Blended; Artist->Animation->GetBoneTransform(Sample,FSkeletonPoseBoneIndex(Index),Context,false);
            Blended.Blend(Pose[I],Sample,ArtistAlpha); Pose[I]=Blended;
        }
    }

    if (!MorphMouth)
    {
        Offset(TEXT("c_jawbone_x"),FVector(0,0,-2.8f*Jaw));
        Offset(TEXT("c_lips_top_x"),FVector(0,0,-1.8f*FMath::Max(0.f,Smile)-3.5f*LipClosure));
        Offset(TEXT("c_lips_bot_x"),FVector(0,0,1.2f*FMath::Max(0.f,Smile)+3.5f*LipClosure));
    }
    for (const FString Side:{FString(TEXT("l")),FString(TEXT("r"))})
    {
        const float Sign=Side==TEXT("l")?1.f:-1.f;
        if (!MorphMouth) Offset(FName(*(TEXT("c_lips_smile_")+Side)),FVector(Sign*(Smile*3.f-Round*1.6f),0,Smile*3.5f));
        if (!AuthoredFace || YawnStrain)
        {
            Offset(FName(*(TEXT("c_eyebrow_full_")+Side)),FVector(0,0,Brows*1.7f*(1-ArtistAlpha)));
            Offset(FName(*(TEXT("c_eyebrow_01_")+Side)),FVector(0,0,-BrowTilt*2.5f*(1-ArtistAlpha)));
            Offset(FName(*(TEXT("c_eyebrow_03_")+Side)),FVector(0,0,BrowTilt*1.2f*(1-ArtistAlpha)));
        }
    }
}
void UMCExpressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMCExpressionComponent,State);
}
