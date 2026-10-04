#include "MCBossFaceComponent.h"
#include "MCBossCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

namespace
{
    enum EBossFaceChannel : int32 { EyesBlink, EyesSquint, BrowAngry, MouthPain, MouthRoar, MouthAngry, MouthSurprise };
    float SafeSetting(float Value, float Default, float Min, float Max)
    {
        return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Default;
    }
    float ReactionEnvelope(double Age, float Duration)
    {
        if (Age<0. || Age>=Duration) return 0.f;
        return FMath::SmoothStep(0.f,.055f,float(Age))*(1.f-FMath::SmoothStep(.10f,Duration,float(Age)));
    }
}

UMCBossFaceComponent::UMCBossFaceComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
    // Gameplay and timestamps already replicate on the owner. No cosmetic RPCs are needed.
    SetIsReplicatedByDefault(false);
    static const FName Names[]={TEXT("Eyes_Blink"),TEXT("Eyes_Squint"),TEXT("Brow_Angry"),TEXT("Mouth_Pain"),
        TEXT("Mouth_Roar"),TEXT("Mouth_Angry"),TEXT("Mouth_Surprise")};
    for (FName Name:Names)
    {
        FMorphChannel& Channel=Channels.AddDefaulted_GetRef();
        Channel.Name=Name;
    }
}

void UMCBossFaceComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode()==NM_DedicatedServer) { SetComponentTickEnabled(false); return; }
    Boss=Cast<AMCBossCharacter>(GetOwner());
    if (!Boss.IsValid()) { SetComponentTickEnabled(false); return; }
    AddTickPrerequisiteActor(Boss.Get());
    RefreshMorphTargets();
    if (Mesh.IsValid()) Mesh->AddTickPrerequisiteComponent(this);
    BlinkRandom.Initialize(int32(GetTypeHash(Boss->GetFName())));
    NextBlinkAt=ServerNow()+BlinkRandom.FRandRange(.5f,2.1f);
}

void UMCBossFaceComponent::RefreshMorphTargets()
{
    if (GetNetMode()==NM_DedicatedServer) return;
    if (!Boss.IsValid()) Boss=Cast<AMCBossCharacter>(GetOwner());
    Mesh=Boss.IsValid()?Boss->GetMesh():nullptr;
    CachedAsset=Mesh.IsValid()?Mesh->GetSkeletalMeshAsset():nullptr;
    for (auto& Channel:Channels)
    {
        Channel.bAvailable=CachedAsset.IsValid() && CachedAsset->FindMorphTarget(Channel.Name)!=nullptr;
        Channel.Weight=0.f;
        if (Channel.bAvailable) Mesh->SetMorphTarget(Channel.Name,0.f);
    }
}

double UMCBossFaceComponent::ServerNow() const
{
    const UWorld* World=GetWorld();
    const AGameStateBase* GameState=World?World->GetGameState():nullptr;
    return GameState?GameState->GetServerWorldTimeSeconds():(World?World->GetTimeSeconds():0.);
}

void UMCBossFaceComponent::PlayRoarExpression(double ServerStartedAt, float Duration)
{
    RoarStartedAt=FMath::IsFinite(ServerStartedAt)?ServerStartedAt:ServerNow();
    RoarSeconds=SafeSetting(Duration,5.f,.1f,15.f);
}

void UMCBossFaceComponent::ClearRoarExpression()
{
    RoarStartedAt=-1000.;
    RoarSeconds=0.f;
}

void UMCBossFaceComponent::ApplyChannel(int32 Index, float Target, float Smoothing)
{
    auto& Channel=Channels[Index];
    if (!Channel.bAvailable) return;
    const float Previous=Channel.Weight;
    Channel.Weight=FMath::Lerp(Previous,FMath::Clamp(Target,0.f,1.f),Smoothing);
    if (Channel.Weight<.0001f) Channel.Weight=0.f;
    if (!FMath::IsNearlyEqual(Channel.Weight,Previous,.00001f)) Mesh->SetMorphTarget(Channel.Name,Channel.Weight);
}

void UMCBossFaceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if (!Boss.IsValid() || !Mesh.IsValid() || GetNetMode()==NM_DedicatedServer) return;
    if (Mesh->GetSkeletalMeshAsset()!=CachedAsset.Get()) RefreshMorphTargets();
    if (!CachedAsset.IsValid()) return;

    const auto& State=Boss->Runtime;
    const double Now=ServerNow();
    const float Dt=FMath::IsFinite(DeltaTime)?FMath::Clamp(DeltaTime,0.f,.25f):0.f;
    const double PreviewAge=Now-State.PreviewStartedAt;
    const bool bDead=State.State==EMCBossState::Dead || State.Health<=0.f || State.AnimationPreview==EMCBossAnimationPreview::Death;
    const bool bAttack=State.State==EMCBossState::Telegraph || State.State==EMCBossState::Attacking || State.State==EMCBossState::Recovering
        || State.AnimationPreview==EMCBossAnimationPreview::PunchLeft || State.AnimationPreview==EMCBossAnimationPreview::PunchRight
        || State.AnimationPreview==EMCBossAnimationPreview::Kick;
    const bool bCombat=State.State!=EMCBossState::Dormant && State.State!=EMCBossState::Dead;
    const float ReactionLength=SafeSetting(HitReactionSeconds,.55f,.1f,2.f);
    Pain=bDead?0.f:ReactionEnvelope(Now-State.HurtStartedAt,ReactionLength);
    if (!bDead && State.AnimationPreview==EMCBossAnimationPreview::Hurt)
        Pain=FMath::Max(Pain,ReactionEnvelope(PreviewAge,ReactionLength));

    double RoarAge=Now-RoarStartedAt;
    float RoarLength=RoarSeconds;
    if (State.AnimationPreview==EMCBossAnimationPreview::Roar)
    {
        RoarAge=PreviewAge;
        const UMCBossProfile* Profile=Boss->GetResolvedProfile();
        const UAnimSequence* Clip=Profile?Profile->RoarAnimation.Get():nullptr;
        RoarLength=Clip?FMath::Max(.5f,Clip->GetPlayLength()):5.f;
    }
    Roar=0.f;
    if (!bDead && RoarLength>.1f && RoarAge>=0. && RoarAge<RoarLength)
    {
        const float Envelope=FMath::SmoothStep(.15f,.6f,float(RoarAge))
            *(1.f-FMath::SmoothStep(RoarLength-.55f,RoarLength,float(RoarAge)));
        Roar=Envelope*(.9f+.1f*FMath::Sin(float(RoarAge)*13.f))*(1.f-Pain);
    }
    Anger=bDead?.12f:FMath::Max(Roar*.95f,(bCombat || bAttack)?SafeSetting(CombatAnger,.78f,0.f,1.f):.12f);
    Anger=FMath::Lerp(Anger,.95f,Pain);

    const float BlinkDuration=SafeSetting(BlinkSeconds,.16f,.05f,.5f);
    if (!bDead && Now>=NextBlinkAt)
    {
        BlinkStartedAt=Now;
        const float Minimum=SafeSetting(MinBlinkInterval,2.4f,1.f,10.f);
        const float Maximum=FMath::Max(Minimum,SafeSetting(MaxBlinkInterval,4.8f,1.f,12.f));
        NextBlinkAt=Now+BlinkRandom.FRandRange(Minimum,Maximum);
    }
    const double BlinkAge=Now-BlinkStartedAt;
    Blink=bDead?.78f:(BlinkAge>=0. && BlinkAge<BlinkDuration?FMath::Sin(float(BlinkAge/BlinkDuration)*UE_PI):0.f);
    Blink*=1.f-Roar;
    const float Squint=bDead?.15f:FMath::Max(Pain*.92f,Anger*.26f);
    const float MouthPainWeight=bDead?.34f:Pain*.95f;
    const float MouthRoarWeight=Roar*.96f;
    // The mouth shapes are complete authored poses. Convex blending avoids applying two jaw openings together.
    const float MouthAngryWeight=bDead?0.f:Anger*(bAttack?.65f:.38f)*(1.f-MouthPainWeight)*(1.f-MouthRoarWeight);
    const float Smoothing=1.f-FMath::Exp(-SafeSetting(ResponseSpeed,20.f,1.f,60.f)*Dt);
    ApplyChannel(EyesBlink,Blink,Smoothing);
    ApplyChannel(EyesSquint,Squint*(1.f-Blink),Smoothing);
    ApplyChannel(BrowAngry,Anger,Smoothing);
    ApplyChannel(MouthPain,MouthPainWeight,Smoothing);
    ApplyChannel(MouthRoar,MouthRoarWeight*(1.f-MouthPainWeight),Smoothing);
    ApplyChannel(MouthAngry,MouthAngryWeight,Smoothing);
    ApplyChannel(MouthSurprise,0.f,Smoothing);
}

void UMCBossFaceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Mesh.IsValid())
    {
        Mesh->RemoveTickPrerequisiteComponent(this);
        for (const auto& Channel:Channels) if (Channel.bAvailable) Mesh->SetMorphTarget(Channel.Name,0.f);
    }
    if (Boss.IsValid()) RemoveTickPrerequisiteActor(Boss.Get());
    Boss.Reset();
    Mesh.Reset();
    CachedAsset.Reset();
    Super::EndPlay(EndPlayReason);
}
