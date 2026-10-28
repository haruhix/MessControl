#include "MCSlipperyFood.h"

#include "MCFoodActor.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCToothCharacter.h"
#include "Components/BoxComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<float> CVarSlipFoodPopSpeed(TEXT("MC.Slip.FoodPopSpeed"),500.f,
    TEXT("Upward velocity in cm/s for food released by a slippery fall; ordinary drops and Q are unaffected."),ECVF_Default);
bool IsDeliveryFood(const AMCFoodActor* Food,const AMCToothCharacter* Tooth)
{
    if(!IsValid(Food) || Food->IsActorBeingDestroyed() || Food->GetWorld()!=Tooth->GetWorld()
        || Food->bBrushTool || Food->FoodData.Kind!=EMCFoodKind::Food || Food->IsNutEncounterFragment()) return false;
    // Native stack pickup normally keeps Phase Free; Carried alone would miss
    // the main delivery stack. Terminal/handoff phases must never count a stale
    // entry while the food is already being swallowed, absorbed or disposed.
    return Food->Phase==EMCFoodPhase::Free || Food->Phase==EMCFoodPhase::Falling
        || Food->Phase==EMCFoodPhase::Carried;
}
}

bool MCSlipperyFood::IsCarrying(const AMCToothCharacter* Tooth)
{
    if(!IsValid(Tooth) || Tooth->IsActorBeingDestroyed()) return false;
    if(Tooth->FoodCollection) for(const auto& Piece:Tooth->FoodCollection->Pieces)
        if(IsDeliveryFood(Piece,Tooth) && Piece->StackCarrier==Tooth) return true;

    const auto HeldByTooth=[Tooth](const AMCFoodActor* Food)
    {
        return IsDeliveryFood(Food,Tooth) && Food->Holders.Contains(Tooth);
    };
    if(HeldByTooth(Tooth->HeldFood)) return true;
    return Tooth->Grip && (HeldByTooth(Tooth->Grip->Frame.Food) || HeldByTooth(Tooth->Grip->Secondary.Food));
}

int32 MCSlipperyFood::ReleaseWithCartoonPop(AMCToothCharacter* Tooth)
{
    if(!IsValid(Tooth) || !Tooth->HasAuthority()) return 0;
    TArray<TWeakObjectPtr<AMCFoodActor>> Load;
    const auto Remember=[&](AMCFoodActor* Food) {
        if(IsDeliveryFood(Food,Tooth)
            && (Food->StackCarrier==Tooth || Food->Holders.Contains(Tooth))) Load.AddUnique(Food);
    };
    if(Tooth->FoodCollection) for(const auto& Food:Tooth->FoodCollection->Pieces) Remember(Food);
    Remember(Tooth->HeldFood);
    if(Tooth->Grip) {Remember(Tooth->Grip->Frame.Food);Remember(Tooth->Grip->Secondary.Food);}
    if(Load.IsEmpty()) return 0;

    // Save real carrier motion before the normal cancellation zeroes it. The
    // animated pickup arc is deliberately not used as projectile energy.
    const FVector Inherited=FVector(Tooth->GetVelocity().X,Tooth->GetVelocity().Y,0.f).GetClampedToMaxSize2D(460.f)*.15f;
    if(Tooth->FoodCollection) Tooth->FoodCollection->Stop(false);
    for(const auto& Ref:Load) if(auto* Food=Ref.Get();Food && Food->Holders.Contains(Tooth)) Food->Release(Tooth);
    Tooth->DropFood();

    const float Authored=CVarSlipFoodPopSpeed.GetValueOnGameThread();
    const float Up=FMath::IsFinite(Authored)?FMath::Clamp(Authored,250.f,650.f):500.f;
    int32 Launched=0;
    for(int32 Index=0;Index<Load.Num();++Index) {
        auto* Food=Load[Index].Get();
        if(!IsDeliveryFood(Food,Tooth) || Food->StackCarrier || !Food->Holders.IsEmpty()
            || !Food->Body || !Food->Body->IsSimulatingPhysics()) continue;
        // Stable per-piece variation creates a small fan without consuming the
        // gameplay random stream or turning this release into a manual Q shot.
        const float Angle=Index*2.39996323f;
        const FVector Fan=(Tooth->GetActorForwardVector()*FMath::Cos(Angle)
            +Tooth->GetActorRightVector()*FMath::Sin(Angle))*55.f;
        FVector Velocity=(Inherited+Fan).GetClampedToMaxSize2D(110.f);
        Velocity.Z=Up+18.f*(Index%3-1);
        Food->ProtectPlayersOnStackRelease(false,2.f);
        Food->Body->SetPhysicsLinearVelocity(Velocity);
        Food->Body->SetPhysicsAngularVelocityInRadians(FVector(2.6f*FMath::Cos(Angle),
            2.6f*FMath::Sin(Angle),(Index%2?1.f:-1.f)*1.2f));
        Food->Body->WakeAllRigidBodies();Food->ForceNetUpdate();++Launched;
    }
    if(Launched) UE_LOG(LogTemp,Log,TEXT("MC_SLIP_FOOD_POP carrier=%s pieces=%d up_cm_s=%.1f"),*GetNameSafe(Tooth),Launched,Up);
    return Launched;
}
