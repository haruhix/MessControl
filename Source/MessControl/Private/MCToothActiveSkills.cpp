#include "MCToothCharacter.h"
#include "MCActiveSkillAction.h"
#include "MCHookSkillAction.h"
#include "MCDriverSkillAction.h"
#include "MCInventoryComponent.h"
#include "MCFoodCollectionComponent.h"
#include "MCGripComponent.h"
#include "MCToothMovementComponent.h"
#include "Components/StaticMeshComponent.h"

AMCActiveSkillAction* AMCToothCharacter::GetActiveSkillPresentation() const
{
    auto* Action=ActiveSkillPresentation.Get();
    return IsValid(Action) && !Action->IsFinished() && Action->Hero()==this ? Action:nullptr;
}
void AMCToothCharacter::SetActiveSkillPresentation(AMCActiveSkillAction* Action)
{
    if(Action && (!IsValid(Action) || Action->Hero()!=this)) return;
    ActiveSkillPresentation=Action;
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
bool AMCToothCharacter::ActiveSkillHandTarget(bool bLeft,FTransform& WorldPose) const
{
    const auto* Action=GetActiveSkillPresentation();
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
