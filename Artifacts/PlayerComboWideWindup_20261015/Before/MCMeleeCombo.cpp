#include "MCMeleeCombo.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace MCMeleeCombo
{
namespace
{
struct FCombo
{
    uint8 ServerStep=0,VisualStep=0,Pending=0,Tool=255;
    double NextAt=-100,ResetAt=-100;
};
TMap<TWeakObjectPtr<const AMCToothCharacter>,FCombo>& States()
{
    // Function-local construction also runs when this translation unit is first
    // introduced by Live Coding; new global dynamic initializers may not run.
    static TMap<TWeakObjectPtr<const AMCToothCharacter>,FCombo> Value;
    return Value;
}
TAutoConsoleVariable<int32>& Debug()
{
    static TAutoConsoleVariable<int32> Value(TEXT("mc.Combo.Debug"),0,TEXT("Log accepted melee combo steps and queued press edges."));
    return Value;
}

float StepDuration(uint8 Tool,uint8 Step)
{
    const float Knife[]={.58f,.62f,.78f},Pickaxe[]={.60f,.64f,.80f};
    return (Tool==uint8(EMCToolSlot::Knife)?Knife:Pickaxe)[FMath::Clamp(int32(Step)-1,0,2)];
}
const FCombo* Find(const AMCToothCharacter* Hero)
{
    const auto* State=Supports(Hero)?States().Find(Hero):nullptr;
    return State && State->Tool==uint8(Hero->Inventory->Selected)?State:nullptr;
}
}

bool Supports(const AMCToothCharacter* Hero)
{
    Debug();
    return IsValid(Hero) && Hero->Inventory && Hero->Inventory->SelectedUpgrade()==EMCToolUpgrade::None
        && (Hero->Inventory->Selected==EMCToolSlot::Knife || Hero->Inventory->Selected==EMCToolSlot::Pickaxe);
}

void Reset(const AMCToothCharacter* Hero) {States().Remove(Hero);}

void QueuePress(AMCToothCharacter* Hero)
{
    if(!Hero->HasAuthority() || !Supports(Hero)) return;
    auto* State=States().Find(Hero);
    if(!State || State->Tool!=uint8(Hero->Inventory->Selected) || State->ServerStep==0 || State->ServerStep>=3) return;
    State->Pending=FMath::Min<uint8>(State->Pending+1,3-State->ServerStep);
    if(Debug().GetValueOnGameThread()) UE_LOG(LogTemp,Display,TEXT("MC_MELEE_COMBO queue step=%d pending=%d"),State->ServerStep,State->Pending);
}

bool ConsumeBufferedPress(AMCToothCharacter* Hero)
{
    if(!Hero->HasAuthority()) return false;
    auto* State=States().Find(Hero);
    if(!State) return false;
    if(!Supports(Hero) || State->Tool!=uint8(Hero->Inventory->Selected) || !Hero->CanWork()
        || Hero->IsRowing() || !Hero->Inventory->ShouldPresentTool()) {
        Reset(Hero);return false;
    }
    const double Now=Hero->GetWorld()->GetTimeSeconds();
    if(Now>State->ResetAt) {Reset(Hero);return false;}
    if(!State->Pending || Now+UE_SMALL_NUMBER<State->NextAt) return false;
    --State->Pending;
    return true;
}

uint8 BeginSwing(AMCToothCharacter* Hero,bool bCombo)
{
    if(!bCombo || !Supports(Hero)) {Reset(Hero);return 0;}
    for(auto It=States().CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
    auto& State=States().FindOrAdd(Hero);
    const double Now=Hero->GetWorld()->GetTimeSeconds();
    const uint8 Tool=uint8(Hero->Inventory->Selected);
    const bool Continue=State.Tool==Tool && State.ServerStep>0 && State.ServerStep<3 && Now<=State.ResetAt;
    if(!Continue) {State.ServerStep=0;State.Pending=0;}
    State.Tool=Tool;State.VisualStep=++State.ServerStep;
    State.Pending=FMath::Min<uint8>(State.Pending,3-State.ServerStep);
    const float Seconds=StepDuration(Tool,State.ServerStep)/Hero->Inventory->RogueAttackRate();
    State.NextAt=Now+Seconds;State.ResetAt=State.NextAt+.38;
    if(Debug().GetValueOnGameThread()) UE_LOG(LogTemp,Display,TEXT("MC_MELEE_COMBO begin step=%d tool=%d pending=%d duration=%.3f"),State.ServerStep,Tool,State.Pending,Seconds);
    return State.ServerStep;
}

void PresentSwing(const AMCToothCharacter* Hero,uint8 ToolSlot,uint8 Step)
{
    auto& State=States().FindOrAdd(Hero);
    State.VisualStep=Step<=3?Step:0;State.Tool=ToolSlot;
}

float Duration(const AMCToothCharacter* Hero)
{
    const auto* State=Find(Hero);
    return State && State->VisualStep?StepDuration(State->Tool,State->VisualStep)/Hero->Inventory->RogueAttackRate():0;
}

float ContactTime(const AMCToothCharacter* Hero)
{
    const auto* State=Find(Hero);
    const float Fraction[]={.36f,.42f,.46f};
    return State && State->VisualStep?Duration(Hero)*Fraction[State->VisualStep-1]:0;
}

FMCMeleeComboPose Sample(const AMCToothCharacter* Hero,float Elapsed)
{
    FMCMeleeComboPose Result;
    const auto* State=Find(Hero);
    if(!State || !State->VisualStep) return Result;
    const float Rate=Hero->Inventory->RogueAttackRate(),T=Elapsed*Rate;
    const float End=StepDuration(State->Tool,State->VisualStep),Contact=ContactTime(Hero)*Rate;
    if(T<0 || T>=End) return Result;
    const float WindEnd=FMath::Max(.07f,Contact-(State->VisualStep==3?.12f:.10f)),FollowEnd=Contact+.10f;
    const float Wind=FMath::SmoothStep(0.f,WindEnd,T),Strike=FMath::SmoothStep(WindEnd,Contact,T);
    const float Follow=FMath::SmoothStep(Contact,FollowEnd,T),Recover=FMath::SmoothStep(FollowEnd,End,T);
    const float Alpha=FMath::SmoothStep(0.f,.055f,T)*(1-Recover);
    Result.bActive=true;
    FVector Ready,Touch,Finish;
    FRotator Back,Hit,Through;
    float LoadTwist=0,FinishTwist=0,Lean=0;
    if(State->VisualStep==1) {
        // Right hand crosses to the LEFT shoulder, then cuts across the body.
        Ready=FVector(10,-108,42);Touch=FVector(82,-14,21);Finish=FVector(35,58,4);
        Back=FRotator(-12,-65,-85);Hit=FRotator(-10,10,-75);Through=FRotator(5,70,-65);
        LoadTwist=-18;FinishTwist=18;Lean=12;
    } else if(State->VisualStep==2) {
        // Descending diagonal: upper RIGHT, across the front, then lower LEFT.
        Ready=FVector(0,40,100);Touch=FVector(70,-43.75f,34);Finish=FVector(24,-125,-48);
        Back=FRotator(-20,70,70);Hit=FRotator(-35,0,55);Through=FRotator(-10,-85,40);
        LoadTwist=17;FinishTwist=-22;Lean=14;
    } else {
        Ready=FVector(-28,8,112);Touch=FVector(82,-10,18);Finish=FVector(48,-14,-38);
        Back=FRotator(98,0,-8);Hit=FRotator(-78,0,0);Through=FRotator(-105,0,5);
        LoadTwist=-3;FinishTwist=4;Lean=18;
    }
    auto BlendRot=[](const FRotator& A,const FRotator& B,float U) {return FQuat::Slerp(A.Quaternion(),B.Quaternion(),U).Rotator();};
    if(T<WindEnd) {
        Result.HandOffset=Ready*Wind;Result.Wrist=BlendRot(FRotator(-12,0,0),Back,Wind);
    } else if(T<Contact) {
        Result.HandOffset=FMath::Lerp(Ready,Touch,Strike);Result.Wrist=BlendRot(Back,Hit,Strike);
    } else {
        Result.HandOffset=FMath::Lerp(Touch,Finish,Follow);Result.Wrist=BlendRot(Hit,Through,Follow);
    }
    if(State->VisualStep==2) {
        const FVector Control(128,-45,42);
        const float U=T<WindEnd?0.f:T<Contact?.5f*Strike:.5f+.5f*Follow;
        if(T>=WindEnd && T<=FollowEnd)
            Result.HandOffset=Ready*FMath::Square(1-U)+Control*(2*(1-U)*U)+Finish*U*U;
        const FVector Tangent=((Control-Ready)*(1-U)+(Finish-Control)*U).GetSafeNormal();
        // Both authored tools have their cutting edge / lower pick point along
        // local -Z, with the handle and blade length along +X. Keep that sharp
        // side leading along the curve, independent of the crown's lean/twist.
        Result.ToolRotation=FRotationMatrix::MakeFromZX(-Tangent,FVector::ForwardVector).ToQuat();
        Result.ToolRotationAlpha=Wind*(1-Recover);
    }
    Result.HandOffset*=1-Recover;
    Result.Wrist=BlendRot(Result.Wrist,FRotator(-12,0,0),Recover);
    Result.Lean=(5+Lean*FMath::SmoothStep(WindEnd*.5f,Contact,T))*Alpha;
    Result.Twist=FMath::Lerp(LoadTwist,FinishTwist,FMath::SmoothStep(WindEnd,FollowEnd,T))*Alpha;
    Result.Side=(State->VisualStep==3?0:State->VisualStep==1?-2.f:2.f)*Alpha;
    Result.Support=(State->VisualStep==3?-28.f:-12.f)*Alpha;
    return Result;
}
}
