#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCPlayerState.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"

bool AMCFoodActor::TryEnterActiveContainer(AActor* Container,AMCToothCharacter* Hero,FGuid Claim)
{
    if(!HasAuthority() || !IsValid(Container) || Container->GetWorld()!=GetWorld() || Container->IsActorBeingDestroyed()
        || !IsValid(Hero) || Hero->GetWorld()!=GetWorld() || Container->GetInstigator()!=Hero || !Claim.IsValid()
        || !Hero->Status || !Hero->Status->IsAlive() || !bFragment || bBrushTool || FoodData.Kind!=EMCFoodKind::Food
        || IsNutEncounterFragment() || ActiveContainer || StackCarrier || !Holders.IsEmpty()
        || (Phase!=EMCFoodPhase::Free && Phase!=EMCFoodPhase::Falling) || bMouthEntry) return false;
    EndManualThrowFlight();
    ActiveContainer=Container;ActiveContainerCarrier=Hero;ActiveContainerClaim=Claim;
    LastHandledBy=Hero->GetPlayerState<AMCPlayerState>();bManuallyThrown=false;
    AttendFood();Phase=EMCFoodPhase::Contained;
    bLandingPending=false;UnreachableSeconds=0;AccessCheckElapsed=0;
    OnRep_Phase();ForceNetUpdate();return true;
}

AMCToothCharacter* AMCFoodActor::GetActiveContainerCarrier() const
{
    return IsInActiveContainer() && IsValid(ActiveContainer) && !ActiveContainer->IsActorBeingDestroyed()
        ? ActiveContainerCarrier.Get():nullptr;
}

bool AMCFoodActor::ExitActiveContainer(AActor* Container,FGuid Claim,FVector Location,FVector Direction,float Speed,bool bThrown)
{
    if(!HasAuthority() || !IsInActiveContainer() || Container!=ActiveContainer || !Claim.IsValid() || Claim!=ActiveContainerClaim
        || Location.ContainsNaN() || Direction.ContainsNaN() || !FMath::IsFinite(Speed)) return false;
    // Clear the claim before enabling physics or callbacks; duplicate releases cannot launch twice.
    AMCToothCharacter* Carrier=ActiveContainerCarrier.Get();
    ActiveContainer=nullptr;ActiveContainerCarrier=nullptr;ActiveContainerClaim.Invalidate();
    Phase=EMCFoodPhase::Free;OnRep_Phase();
    SetActorLocation(Location,false,nullptr,ETeleportType::TeleportPhysics);
    PreviousPhysicsLocation=Location;NutThrowSweepStart=Location;
    ProtectPlayersOnStackRelease(bThrown,.75f);
    if(bThrown && IsValid(Carrier) && Carrier->GetWorld()==GetWorld()) LaunchManualThrow(Carrier,Direction,Speed);
    else if(Body->IsSimulatingPhysics()) {
        Body->SetPhysicsLinearVelocity(Direction.GetSafeNormal()*FMath::Clamp(Speed,0.f,600.f));
        Body->WakeAllRigidBodies();
    }
    UnreachableSeconds=0;AccessCheckElapsed=0;ForceNetUpdate();return true;
}
