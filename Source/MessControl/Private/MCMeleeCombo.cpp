#include "MCMeleeCombo.h"
#include "MCToothCharacter.h"
#include "MCInventoryComponent.h"
#include "MCDataAssets.h"
#include "MCWeaponImpact.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace MCMeleeCombo
{
namespace
{
constexpr uint8 FoodChopStep=4;
constexpr float AnimationSpeed=2.f;
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

float AnimationRate(const AMCToothCharacter* Hero)
{
    return Hero->Inventory->RogueAttackRate()*AnimationSpeed;
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
    State.NextAt=Now+Seconds;
    // The final authored follow-through outlasts the attack cooldown. Keep
    // its visual state alive through recovery; a fresh press still starts a
    // new combo immediately because a third step can never be continued.
    const float VisualRecovery=State.ServerStep==3?(38.f/24.f+.25f)/Hero->Inventory->RogueAttackRate():0.f;
    State.ResetAt=Now+FMath::Max(Seconds+.38f,VisualRecovery);
    if(Debug().GetValueOnGameThread()) UE_LOG(LogTemp,Display,TEXT("MC_MELEE_COMBO begin step=%d tool=%d pending=%d duration=%.3f"),State.ServerStep,Tool,State.Pending,Seconds);
    return State.ServerStep;
}

uint8 BeginFoodChop(AMCToothCharacter* Hero)
{
    if(!Supports(Hero)) {Reset(Hero);return 0;}
    for(auto It=States().CreateIterator();It;++It) if(!It.Key().IsValid()) It.RemoveCurrent();
    auto& State=States().FindOrAdd(Hero);
    State=FCombo(); // Food uses one overhead pose and never retains combat presses.
    State.Tool=uint8(Hero->Inventory->Selected);State.VisualStep=FoodChopStep;
    const float Seconds=StepDuration(State.Tool,FoodChopStep)/Hero->Inventory->RogueAttackRate();
    State.NextAt=Hero->GetWorld()->GetTimeSeconds()+Seconds;State.ResetAt=State.NextAt+.38;
    if(Debug().GetValueOnGameThread()) UE_LOG(LogTemp,Display,TEXT("MC_MELEE_COMBO food_chop tool=%d duration=%.3f"),State.Tool,Seconds);
    return FoodChopStep;
}

void PresentSwing(const AMCToothCharacter* Hero,uint8 ToolSlot,uint8 Step)
{
    auto& State=States().FindOrAdd(Hero);
    State.VisualStep=Step<=FoodChopStep?Step:0;State.Tool=ToolSlot;
}

bool IsFoodChop(const AMCToothCharacter* Hero)
{
    const auto* State=Find(Hero);
    return State && State->VisualStep==FoodChopStep;
}

uint8 VisualStep(const AMCToothCharacter* Hero)
{
    const auto* State=Find(Hero);
    return State?State->VisualStep:0;
}

bool WeaponPose(const AMCToothCharacter* Hero,FTransform& OutPose,float& OutAlpha)
{
    OutAlpha=0;
    const uint8 Step=VisualStep(Hero);
    if(Step<1 || Step>4 || !Hero->AnimationProfile || !Hero->CanWork()
        || !Hero->Inventory->ShouldPresentTool()) return false;
    const auto* Profile=Hero->AnimationProfile.Get();
    const UAnimSequence* Clip=Step==FoodChopStep?Profile->MeleeFoodHit.Get():Profile->MeleeCombo.Get();
    if(!Clip) return false;
    const float Rate=AnimationRate(Hero);
    const float T=MCWeaponImpact::VisualSwingElapsed(Hero)*Rate;
    const float Span[]={10.f/24.f,10.f/24.f,38.f/24.f,13.f/24.f};
    const float Start[]={0.f,10.f/24.f,20.f/24.f,0.f};
    const float End=Step==3?Span[2]:Duration(Hero)*Hero->Inventory->RogueAttackRate();
    if(T<0 || End<=0 || T>=End+VisualRecoverySpan) return false;
    const FAnimExtractContext Context(FMath::Min(double(Clip->GetPlayLength()),double(Start[Step-1]+Span[Step-1]*FMath::Min(T/End,1.f))),false);
    if(Clip->EvaluateCurveData(TEXT("MCWeaponAuthored"),Context)<.5f) return false;
    auto Curve=[&](const TCHAR* Name) {return Clip->EvaluateCurveData(FName(Name),Context);};
    const FVector Location(Curve(TEXT("MCWeaponX")),Curve(TEXT("MCWeaponY")),Curve(TEXT("MCWeaponZ")));
    FQuat Rotation(Curve(TEXT("MCWeaponQX")),Curve(TEXT("MCWeaponQY")),Curve(TEXT("MCWeaponQZ")),Curve(TEXT("MCWeaponQW")));
    const FVector Scale(Curve(TEXT("MCWeaponSX")),Curve(TEXT("MCWeaponSY")),Curve(TEXT("MCWeaponSZ")));
    if(Location.ContainsNaN() || Rotation.ContainsNaN() || Scale.ContainsNaN() || Rotation.SizeSquared()<.01f) return false;
    Rotation.Normalize();OutPose=FTransform(Rotation,Location,Scale);
    OutAlpha=FMath::SmoothStep(0.f,VisualBlendInSpan,T)*(1-FMath::SmoothStep(End,End+VisualRecoverySpan,T));
    return true;
}

float Duration(const AMCToothCharacter* Hero)
{
    const auto* State=Find(Hero);
    return State && State->VisualStep?StepDuration(State->Tool,State->VisualStep)/Hero->Inventory->RogueAttackRate():0;
}

float ContactTime(const AMCToothCharacter* Hero)
{
    const auto* State=Find(Hero);
    // Follow the authored contact in each stroke while retaining the existing
    // weapon cooldowns and damage rate.
    const float Fraction[]={.60f,.70f,14.f/38.f,7.f/13.f};
    if(!State || !State->VisualStep) return 0;
    if(State->VisualStep==3) return (14.f/24.f)/AnimationRate(Hero);
    return Duration(Hero)*Fraction[FMath::Clamp(int32(State->VisualStep)-1,0,3)]/AnimationSpeed;
}

FVector2D TrailWindow(const AMCToothCharacter* Hero)
{
    const auto* State=Find(Hero);
    if(!State || !State->VisualStep) return FVector2D(-1,-1);
    const float Rate=AnimationRate(Hero),Contact=ContactTime(Hero);
    const uint8 Step=FMath::Min<uint8>(State->VisualStep,3);
    return FVector2D(FMath::Max(.07f,Contact*Rate-(Step==3?.12f:.10f))/Rate,Contact+.10f/Rate);
}

FMCMeleeComboPose Sample(const AMCToothCharacter* Hero,float Elapsed)
{
    FMCMeleeComboPose Result;
    const auto* State=Find(Hero);
    if(!State || !State->VisualStep) return Result;
    // Food's distinct event tag reuses the overhead pose, without becoming a
    // combat finisher or allowing another press to continue a previous chain.
    const uint8 Step=FMath::Min<uint8>(State->VisualStep,3);
    const float Rate=AnimationRate(Hero),T=Elapsed*Rate;
    const float End=StepDuration(State->Tool,State->VisualStep),Contact=ContactTime(Hero)*Rate;
    if(T<0 || T>=End) return Result;
    const FVector2D Window=TrailWindow(Hero)*Rate;
    const float WindEnd=Window.X,FollowEnd=Window.Y;
    // Let the second stroke settle briefly in its fully drawn-back pose.
    const float ReadyAt=Step==2?FMath::Max(.06f,WindEnd-.035f):WindEnd;
    const float Wind=FMath::SmoothStep(0.f,ReadyAt,T),Strike=FMath::SmoothStep(WindEnd,Contact,T);
    const float Follow=FMath::SmoothStep(Contact,FollowEnd,T),Recover=FMath::SmoothStep(FollowEnd,End,T);
    const float Alpha=FMath::SmoothStep(0.f,.055f,T)*(1-Recover);
    Result.bActive=true;
    FVector Ready,Touch,Finish;
    FRotator Back,Hit,Through;
    float LoadTwist=0,FinishTwist=0,Lean=0;
    if(Step==1) {
        // Right hand crosses to the LEFT shoulder, then cuts across the body.
        Ready=FVector(10,-108,42);Touch=FVector(82,-14,21);Finish=FVector(35,58,4);
        Back=FRotator(-12,-65,-85);Hit=FRotator(-10,10,-75);Through=FRotator(5,70,-65);
        LoadTwist=-18;FinishTwist=18;Lean=12;
    } else if(Step==2) {
        // Draw back behind the right shoulder, then sweep a broad descending
        // diagonal across the front into a low left follow-through.
        Ready=FVector(-64,74,110);Touch=FVector(70,-43.75f,34);Finish=FVector(24,-145,-58);
        Back=FRotator(-20,70,70);Hit=FRotator(-35,0,55);Through=FRotator(-10,-85,40);
        LoadTwist=25;FinishTwist=-26;Lean=14;
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
    if(Step==2) {
        // Widen the arc while retaining its existing contact point at U=.5.
        const FVector Control(160,-52,42);
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
    Result.Side=(Step==3?0:Step==1?-2.f:2.f)*Alpha;
    Result.Support=(Step==3?-28.f:-12.f)*Alpha;
    return Result;
}
}
