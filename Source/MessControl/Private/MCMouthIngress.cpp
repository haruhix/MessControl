#include "MCGameState.h"

#include "MCCoffeeFlood.h"
#include "MCFoodActor.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "EngineUtils.h"

namespace
{
const FName MouthOpenMorph(TEXT("open"));
}

USkeletalMeshComponent* AMCGameState::FindMouthEntrance()
{
    if(auto* Mesh=MouthEntrance.Get(); Mesh && Mesh->IsRegistered()) return Mesh;
    for(TActorIterator<ASkeletalMeshActor> It(GetWorld());It;++It)
    {
        auto* Mesh=It->GetSkeletalMeshComponent();
        const auto* Asset=Mesh?Mesh->GetSkeletalMeshAsset():nullptr;
        if(Asset && Mesh->IsRegistered() && Asset->GetPathName()==TEXT("/Game/FromBlender12/SK_mouth_in.SK_mouth_in"))
        {
            MouthEntrance=Mesh;
            return Mesh;
        }
    }
    return nullptr;
}

void AMCGameState::NotifyFrontFoodIngress(const AMCFoodActor& Food,const FVector& LaunchVelocity)
{
    if(!HasAuthority() || !Food.IsMouthEntryActive() || LaunchVelocity.X<=UE_SMALL_NUMBER) return;
    if(const auto* Mesh=FindMouthEntrance())
    {
        const double FarEdge=Mesh->Bounds.Origin.X+Food.Body->Bounds.BoxExtent.X;
        const double ClearTime=(FarEdge-Food.GetActorLocation().X)/LaunchVelocity.X;
        HoldMouthEntranceOpen(FMath::Clamp(ClearTime,0.,3.)+.24);
    }
}

void AMCGameState::NotifyFrontDrinkIngress(const AMCCoffeeFlood& Drink)
{
    if(!HasAuthority() || !Drink.IsActive() || !Drink.bRiverFlood || Drink.RiverSpeed<=UE_SMALL_NUMBER) return;
    if(const auto* Mesh=FindMouthEntrance())
    {
        const double Along=FVector::DotProduct(Mesh->Bounds.Origin-Drink.RiverOrigin,Drink.RiverDirection);
        // Match the river's start delay and let the soft trailing edge clear the lips.
        const double ClearTime=.35+(FMath::Max(0.,Along)+Drink.RiverWidth+120.)/Drink.RiverSpeed;
        HoldMouthEntranceOpen(ClearTime-Drink.PhaseTime());
    }
}

void AMCGameState::HoldMouthEntranceOpen(double Seconds)
{
    if(!FMath::IsFinite(Seconds) || Seconds<=0.) return;
    const double Until=GetServerWorldTimeSeconds()+Seconds;
    if(Until<=MouthIngressOpenUntil) return;
    MouthIngressOpenUntil=Until;
    OnRep_MouthIngressOpenUntil();
    ForceNetUpdate();
}

void AMCGameState::OnRep_MouthIngressOpenUntil()
{
    if(GetNetMode()!=NM_DedicatedServer && MouthIngressOpenUntil>GetServerWorldTimeSeconds())
        SetActorTickEnabled(true);
}

void AMCGameState::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Native OnRep still estimates and smooths the server clock. Bound its
    // accumulated history so loading stalls do not bias short gameplay events
    // for the next 250 samples. Keep recent samples weighted, never invent time.
    if(!HasAuthority() && NumServerWorldTimeSecondsDeltas>8) {
        SumServerWorldTimeSecondsDelta=SumServerWorldTimeSecondsDelta/NumServerWorldTimeSecondsDeltas*8;
        NumServerWorldTimeSecondsDeltas=8;
    }
    const bool bOpen=MouthIngressOpenUntil>GetServerWorldTimeSeconds();
    if(!HasAuthority() && !bOpen && MouthIngressProgress<=0.f) return;
    if(auto* Mesh=FindMouthEntrance())
    {
        MouthIngressProgress=FMath::FInterpConstantTo(MouthIngressProgress,bOpen?1.f:0.f,DeltaSeconds,bOpen?10.f:3.f);
        const float Weight=FMath::SmoothStep(0.f,1.f,MouthIngressProgress);
        if(Mesh->GetMorphTarget(MouthOpenMorph)!=Weight)
        {
            Mesh->SetMorphTarget(MouthOpenMorph,Weight);
            // The placed morph-only mesh has no animation instance to publish overrides.
            if(!Mesh->GetAnimInstance() && !Mesh->GetPostProcessInstance())
            {
                Mesh->RefreshBoneTransforms();
            }
        }
    }
    else MouthIngressProgress=0.f;
    if(HasAuthority() && !bOpen && MouthIngressProgress<=0.f) SetActorTickEnabled(false);
}
