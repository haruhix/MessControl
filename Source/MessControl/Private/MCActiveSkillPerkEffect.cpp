#include "MCActiveSkillPerkEffect.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool UMCActiveSkillPerkEffect::CanActivate_Implementation(FName,const FMCPerkDefinition&) const
{return ActionClass && !ActionClass->HasAnyClassFlags(CLASS_Abstract);}

AMCActiveSkillAction* UMCActiveSkillPerkEffect::PrepareAction(FName ID,const FMCPerkDefinition& Row,
    const FMCActiveSkillInputHandle& Handle) const
{
    const auto* State=OwnerComponent?Cast<AMCPlayerState>(OwnerComponent->GetOwner()):nullptr;
    auto* H=State?Cast<AMCToothCharacter>(State->GetPawn()):nullptr;
    if(!State || !State->HasAuthority() || !H || !GetWorld() || !Handle.IsValid() || !CanActivate(ID,Row)) return nullptr;
    const FTransform Transform(H->GetActorRotation(),H->GetActorLocation());
    auto* Action=GetWorld()->SpawnActorDeferred<AMCActiveSkillAction>(ActionClass,Transform,H->GetController(),H,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Action) return nullptr;
    Action->InitializeAction(OwnerComponent,H,ID,Handle,Row.Parameters);
    Action->FinishSpawning(Transform);
    if(!IsValid(Action) || !Action->CanStart()) {
#if WITH_EDITOR
        if(FParse::Param(FCommandLine::Get(),TEXT("MCActiveSkillReview")))
            UE_LOG(LogTemp,Display,TEXT("MC_ACTIVE_REJECT stage=PerkEffect.PrepareAction gate=CanStart slot=%d actionValid=%d baseCanStart=%d committed=%d finished=%d sourcePawnMatches=%d sourceStateMatches=%d"),
                Handle.SlotIndex,IsValid(Action),IsValid(Action) && Action->AMCActiveSkillAction::CanStart(),
                IsValid(Action) && Action->IsCommitted(),IsValid(Action) && Action->IsFinished(),
                IsValid(Action) && Action->Hero()==H,IsValid(Action) && Action->SourceState()==State);
#endif
        if(IsValid(Action)) Action->Finish(EMCActiveSkillEndReason::InputCanceled);
        return nullptr;
    }
    return Action;
}
