#include "MCToothCharacter.h"
#include "MCActiveSkillAction.h"
#include "MCHookSkillAction.h"
#include "MCPlayerController.h"
#include "MCDriverSkillAction.h"
#include "MCInventoryComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCToothMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

AMCActiveSkillAction* AMCToothCharacter::GetActiveSkillPresentation() const
{
    auto* Action=ActiveSkillPresentation.Get();
    return IsValid(Action) && !Action->IsFinished() && Action->Hero()==this ? Action:nullptr;
}
void AMCToothCharacter::SetActiveSkillPresentation(AMCActiveSkillAction* Action)
{
    if(Action && (!IsValid(Action) || Action->Hero()!=this)) return;
    ActiveSkillPresentation=Action;
    if(Action && SkillAimProp) SkillAimProp->SetVisibility(false);
}
bool AMCToothCharacter::IsSkillAimHandPresented(bool bLeft) const
{
    return SkillAimProp && SkillAimProp->IsVisible() && (!bLeft || SkillAimPose.bHasLeftHand);
}
void AMCToothCharacter::UpdateSkillAimPresentation()
{
    const auto* PC=Cast<AMCPlayerController>(GetController());
    UStaticMesh* ReadyMesh=nullptr;
    FMCActiveSkillAimPose Pose;
    if(GetActiveSkillPresentation() || !PC || !PC->ReadSkillAimPose(ReadyMesh,Pose)) {
        if(SkillAimProp) SkillAimProp->SetVisibility(false);
        return;
    }
    SkillAimPose=Pose;
    if(!SkillAimProp) {
        SkillAimProp=NewObject<UStaticMeshComponent>(this,TEXT("SkillAimProp"));
        SkillAimProp->SetupAttachment(GetRootComponent());
        SkillAimProp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SkillAimProp->SetGenerateOverlapEvents(false);
        SkillAimProp->SetVisibility(false);
        SkillAimProp->SetStaticMesh(ReadyMesh);
        SkillAimProp->RegisterComponent();
    }
    if(SkillAimProp->GetStaticMesh()!=ReadyMesh) SkillAimProp->SetStaticMesh(ReadyMesh);
    SkillAimProp->SetWorldTransform(SkillAimPose.PropLocal*FTransform(GetActorQuat(),GetActorLocation()));
    SkillAimProp->SetVisibility(true);
}
void AMCToothCharacter::SuspendOrdinaryWorkForSkill()
{
    // Unlike CancelGameplayInput this must NOT route a cancel back to the skill being committed.
    if(!HasAuthority() && !IsLocallyControlled()) return;
    StopRowing();StopThrowItem();StopBrace();
    if(Inventory) Inventory->CancelUpgradeUse();
    bSharedPrimaryInputHeld=false;bSharedHandleInputHeld=false;
    StopPrimary();StopBrush();StopHandle();
    if(HasAuthority() && FoodCollection) FoodCollection->Stop();
    if(Grip) {Grip->ReleaseBrace();Grip->ReleasePlayer();}
    ResetContact();
}
void AMCToothCharacter::SuspendToolInputForSkillAim()
{
    const auto* PC=Cast<APlayerController>(GetController());
    const bool bPrimaryDown=PC && (PC->IsInputKeyDown(EKeys::LeftMouseButton)
        || PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder) || PC->IsInputKeyDown(EKeys::Gamepad_RightTrigger));
    if(bPrimaryDown || bPrimaryHeld || bSharedPrimaryInputHeld || bRowingInputHeld || bRowing) {
        bSharedPrimaryInputHeld=false;
        StopPrimary();
    }
    if(Grip && (Grip->IsBraceInputHeld() || (PC && (PC->IsInputKeyDown(EKeys::RightMouseButton)
        || PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder))))) StopBrace();
}
bool AMCToothCharacter::ActiveSkillHandTarget(bool bLeft,FTransform& WorldPose) const
{
    const auto* Action=GetActiveSkillPresentation();
    if(!Action && IsSkillAimHandPresented(bLeft)) {
        WorldPose=SkillAimPose.HandsLocal[bLeft?0:1]*FTransform(GetActorQuat(),GetActorLocation());
        WorldPose.SetScale3D(FVector::OneVector);
        return !WorldPose.ContainsNaN();
    }
    if(!Action || !Action->PropMesh || !Action->PropMesh->GetStaticMesh()) return false;
    if(const auto* Hook=Cast<AMCHookSkillAction>(Action)) {
        WorldPose=Hook->GetCasterHandFrame(bLeft);
        return !WorldPose.ContainsNaN();
    }
    if(const auto* Driver=Cast<AMCDriverSkillAction>(Action)) {
        WorldPose=Driver->GetHandFrame(bLeft);
        return !WorldPose.ContainsNaN();
    }
    return Action->GetPresentationHandTarget(bLeft,WorldPose);
}
