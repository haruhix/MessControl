#include "MCKatanaInput.h"
#include "MCKatanaPlayback.h"

#include "MCToothCharacter.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"

namespace MCKatanaInput
{
namespace
{
struct FState
{
    double PressedAt=-1.;
    TArray<uint8,TInlineAllocator<MaxPending>> Pending;
    MCKatanaSwing::EVariant LastNormal=MCKatanaSwing::EVariant::None;
    float FacingYaw=0.f,ControlYaw=0.f,OrbitYaw=0.f;
    bool bFacing=false;
    bool bAutoChargeQueued=false;
};
TMap<TWeakObjectPtr<const AMCToothCharacter>,FState>& States()
{
    // Construct on first use, including when introduced through Live Coding.
    static TMap<TWeakObjectPtr<const AMCToothCharacter>,FState> Value;
    return Value;
}
FState& State(const AMCToothCharacter* Hero)
{
    for(auto It=States().CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
    return States().FindOrAdd(Hero);
}
}

void Press(const AMCToothCharacter* Hero,double Now)
{
    auto& Value=State(Hero);
    Value.PressedAt=Now;
    Value.bAutoChargeQueued=false;
}
double PressedAt(const AMCToothCharacter* Hero)
{
    const auto* Value=States().Find(Hero);
    return Value?Value->PressedAt:-1.;
}
void Release(const AMCToothCharacter* Hero)
{
    if(auto* Value=States().Find(Hero)) Value->PressedAt=-1.;
}
bool Queue(const AMCToothCharacter* Hero,bool bCharged)
{
    auto& Value=State(Hero);
    if(Value.Pending.Num()>=MaxPending) return false;
    Value.Pending.Add(bCharged?1:0);
    return true;
}
bool Consume(const AMCToothCharacter* Hero,bool& bCharged)
{
    auto* Value=States().Find(Hero);
    if(!Value || Value->Pending.IsEmpty()) return false;
    bCharged=Value->Pending[0]!=0;
    Value->Pending.RemoveAt(0,1,EAllowShrinking::No);
    return true;
}
bool QueueAutoCharge(const AMCToothCharacter* Hero,double Now)
{
    auto* Value=States().Find(Hero);
    if(!Value || Value->PressedAt<0. || Value->bAutoChargeQueued
        || Now-Value->PressedAt<MCKatanaSwing::ChargeThreshold || Value->Pending.Num()>=MaxPending) return false;
    Value->Pending.Add(1);
    Value->bAutoChargeQueued=true;
    return true;
}
bool IsPreparingCharge(const AMCToothCharacter* Hero)
{
    const auto* Value=States().Find(Hero);
    return Value && Value->PressedAt>=0. && !Value->bAutoChargeQueued;
}
MCKatanaSwing::EVariant ChooseNormal(const AMCToothCharacter* Hero)
{
    auto& Value=State(Hero);
    TArray<MCKatanaSwing::EVariant,TInlineAllocator<4>> Choices;
    for(const auto Variant:MCKatanaSwing::NormalVariants())
        if(Variant!=Value.LastNormal) Choices.Add(Variant);
    Value.LastNormal=Choices[FMath::RandRange(0,Choices.Num()-1)];
    return Value.LastNormal;
}
void ArmFacing(const AMCToothCharacter* Hero,const FVector& Target)
{
    auto& Value=State(Hero);
    Value.bFacing=false;
    const FVector Offset=(Target-Hero->GetActorLocation()).GetSafeNormal2D();
    if(Offset.IsNearlyZero()) return;
    const float Yaw=Hero->GetActorRotation().Yaw;
    const float Error=FMath::FindDeltaAngleDegrees(Yaw,Offset.Rotation().Yaw);
    // Only assist a target already in the player's forward sector. The total
    // correction is capped, and never changes the camera or capsule location.
    if(FMath::Abs(Error)>40.f) return;
    Value.FacingYaw=Yaw+FMath::Clamp(Error,-20.f,20.f);
    Value.ControlYaw=Hero->GetController()?Hero->GetController()->GetControlRotation().Yaw:0.f;
    Value.OrbitYaw=Hero->bManualCameraOrbit?Hero->CameraOrbitYaw:Hero->Camera->GetComponentRotation().Yaw;
    Value.bFacing=true;
}
bool WantsFacing(const AMCToothCharacter* Hero,FVector& Direction)
{
    auto* Value=States().Find(Hero);
    if(!Value || !Value->bFacing) return false;
    const auto Variant=Hero->GetKatanaSwingVariant();
    const auto Cuts=MCKatanaSwing::PlaybackCuts(Variant);
    if(Cuts.IsEmpty()) return false;
    const float Time=Hero->GetKatanaSwingElapsed()*Hero->GetKatanaSwingPlayRate()+MCKatanaSwing::PlaybackStart(Variant);
    // Finish steering before the cutting edge becomes active. Tracking during
    // a cut would rotate its contact plane and make a visible slash misleading.
    if(Time>=Cuts[0].Start) return false;
    if(Hero->bManualCameraOrbit && FMath::Abs(FMath::FindDeltaAngleDegrees(Value->OrbitYaw,Hero->CameraOrbitYaw))>6.f) {
        Value->bFacing=false;return false;
    }
    if(const auto* Controller=Hero->GetController();Controller
        && FMath::Abs(FMath::FindDeltaAngleDegrees(Value->ControlYaw,Controller->GetControlRotation().Yaw))>6.f) {
        Value->bFacing=false;return false;
    }
    Direction=FRotator(0,Value->FacingYaw,0).Vector();
    const FVector Intent=Hero->GetCharacterMovement()->GetCurrentAcceleration().GetSafeNormal2D();
    if(!Intent.IsNearlyZero() && FVector::DotProduct(Intent,Direction)<.5f) {
        Value->bFacing=false;return false;
    }
    return true;
}
void Reset(const AMCToothCharacter* Hero) {States().Remove(Hero);}
}
