#include "MCActiveSkillAction.h"
#include "MCPerkComponent.h"
#include "MCPlayerState.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
bool SkillTickDependsOn(const FTickFunction& Tick,const FTickFunction& Required)
{
    TArray<const FTickFunction*> Pending;Pending.Add(&Tick);
    TSet<const FTickFunction*> Visited;
    while(!Pending.IsEmpty()) {
        const FTickFunction* Current=Pending.Pop(EAllowShrinking::No);
        if(Current==&Required) return true;
        if(Visited.Contains(Current)) continue;
        Visited.Add(Current);
        for(const FTickPrerequisite& Edge:Current->GetPrerequisites())
            if(const FTickFunction* Next=Edge.Get()) Pending.Add(Next);
    }
    return false;
}
}

AMCActiveSkillAction::AMCActiveSkillAction()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=false;
    bReplicates=false;SetReplicateMovement(true);
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("SkillRoot"));SetRootComponent(SceneRoot);
    PropMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));PropMesh->SetupAttachment(SceneRoot);
    PropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);PropMesh->SetGenerateOverlapEvents(false);
    LoopComponent=CreateDefaultSubobject<UNiagaraComponent>(TEXT("SkillLoop"));LoopComponent->SetupAttachment(SceneRoot);
    LoopComponent->SetAbsolute(false,false,true); // User.Radius remains world centimetres under scaled prop parents.
    LoopComponent->SetAutoActivate(false);LoopComponent->SetAutoDestroy(false);
    SetActorHiddenInGame(true);
}
void AMCActiveSkillAction::InitializeAction(UMCPerkComponent* OwnerPerks,AMCToothCharacter* Pawn,FName ID,
    const FMCActiveSkillInputHandle& Handle,const TMap<FName,float>& Values)
{
    check(!bInitialized && !bCommitted);
    SourcePlayerState=OwnerPerks?Cast<AMCPlayerState>(OwnerPerks->GetOwner()):nullptr;
    SourcePawn=Pawn;PerkID=ID;InputHandle=Handle;ActionID=FGuid::NewGuid();bInitialized=true;
    TArray<FName> Keys;Values.GetKeys(Keys);Keys.Sort(FNameLexicalLess());
    for(FName Key:Keys) if(!Key.IsNone() && FMath::IsFinite(Values[Key]) && InitialParameters.Num()<128) {
        FMCActiveSkillParameter Entry;Entry.Key=Key;Entry.Value=Values[Key];InitialParameters.Add(Entry);Parameters.Add(Key,Entry.Value);
    }
}
AMCToothCharacter* AMCActiveSkillAction::Hero() const {return IsValid(SourcePawn)?SourcePawn.Get():nullptr;}
AMCPlayerState* AMCActiveSkillAction::SourceState() const {return IsValid(SourcePlayerState)?SourcePlayerState.Get():nullptr;}
UMCPerkComponent* AMCActiveSkillAction::Perks() const {return SourceState()?SourceState()->Perks.Get():nullptr;}
float AMCActiveSkillAction::Param(FName Key,float Fallback) const
{
    if(const float* Value=Parameters.Find(Key);Value && FMath::IsFinite(*Value)) return *Value;
    if(const auto* Entry=InitialParameters.FindByPredicate([Key](const auto& P){return P.Key==Key;});Entry && FMath::IsFinite(Entry->Value)) return Entry->Value;
    return Fallback;
}
double AMCActiveSkillAction::Now() const
{const auto* W=GetWorld();return W?(W->GetGameState()?W->GetGameState()->GetServerWorldTimeSeconds():W->GetTimeSeconds()):0.;}
float AMCActiveSkillAction::GetPresentationAge() const {return bCommitted?float(FMath::Max(0.,Now()-StartedAt)):0.f;}
float AMCActiveSkillAction::GetPresentationReleaseAge() const
{return PresentationReleasedAt>=0?float(FMath::Max(0.,Now()-PresentationReleasedAt)):-1.f;}
void AMCActiveSkillAction::MarkPresentationReleased()
{
    if(HasAuthority() && PresentationReleasedAt<0) {PresentationReleasedAt=Now();ForceNetUpdate();}
}
bool AMCActiveSkillAction::GetPresentationHandTarget(bool bLeft,FTransform& WorldPose) const
{
    const auto* H=Hero();
    if(!bCommitted || bFinished || !H || !H->Status || !H->Status->IsAlive()
        || !PropMesh || !PropMesh->GetStaticMesh() || !PropMesh->IsVisible()
        || !(bLeft?bHasLeftGrip:bHasRightGrip)) return false;
    FTransform Local=bLeft?LeftGripTransform:RightGripTransform;
    const FString Prefix=bLeft?TEXT("LeftGrip"):TEXT("RightGrip");
    const FVector P=Local.GetLocation();const FRotator R=Local.Rotator();
    Local.SetLocation(FVector(Param(FName(*(Prefix+TEXT("X"))),P.X),Param(FName(*(Prefix+TEXT("Y"))),P.Y),Param(FName(*(Prefix+TEXT("Z"))),P.Z)));
    Local.SetRotation(FRotator(Param(FName(*(Prefix+TEXT("Pitch"))),R.Pitch),Param(FName(*(Prefix+TEXT("Yaw"))),R.Yaw),
        Param(FName(*(Prefix+TEXT("Roll"))),R.Roll)).Quaternion());
    WorldPose=Local*PropMesh->GetComponentTransform();WorldPose.SetScale3D(FVector::OneVector);
    WorldPose.NormalizeRotation();return !WorldPose.ContainsNaN();
}
bool AMCActiveSkillAction::CanStart() const
{
    const auto* H=Hero();const auto* S=SourceState();
    return HasAuthority() && bInitialized && !bCommitted && !bFinished && InputHandle.IsValid() && Perks()
        && H && S && S->GetPawn()==H && H->GetPlayerState<AMCPlayerState>()==S
        && H->Status && H->Status->IsAlive() && H->CanWork()
        && (!IsValid(H->GetActiveSkillPresentation()) || H->GetActiveSkillPresentation()==this);
}
void AMCActiveSkillAction::BeginPlay() {Super::BeginPlay();ApplyPresentation();}
void AMCActiveSkillAction::ApplyPresentation()
{
    if(PropMesh && !PropAsset.IsNull()) {
        PropMesh->SetStaticMesh(PropAsset.LoadSynchronous());PropMesh->SetRelativeTransform(PropTransform);
    }
    SetActorHiddenInGame(!bCommitted);
    if(LoopComponent && !LoopVFX.IsNull() && !LoopComponent->GetAsset()) LoopComponent->SetAsset(LoopVFX.LoadSynchronous());
}
void AMCActiveSkillAction::StartCommitted()
{
    check(HasAuthority() && bInitialized && !bCommitted && !bFinished);
    bCommitted=true;StartedAt=Now();SetReplicates(true);SetActorTickEnabled(true);ApplyPresentation();ClaimPresentation();ForceNetUpdate();
}
void AMCActiveSkillAction::OnRep_Committed() {ApplyPresentation();ApplyPresentationLease();OnRep_Loop();SetActorTickEnabled(bCommitted);}
void AMCActiveSkillAction::ClaimPresentation()
{
    if(bFinished) return;
    if(HasAuthority()) bWantsPresentation=true;
    ApplyPresentationLease();ForceNetUpdate();
}
void AMCActiveSkillAction::ReleasePresentation()
{
    MarkPresentationReleased();
    if(HasAuthority()) bWantsPresentation=false;
    ReleaseAppliedPresentationLease();ForceNetUpdate();
}
void AMCActiveSkillAction::ReleaseAppliedPresentationLease()
{
    // Remove our exact edge even when a replacement already owns the pawn.
    ReleasePresentationTickPrerequisite();
    if(auto* H=AppliedPresentationPawn.Get();H && H->GetActiveSkillPresentation()==this) H->SetActiveSkillPresentation(nullptr);
    AppliedPresentationPawn.Reset();
}
void AMCActiveSkillAction::ReleasePresentationTickPrerequisite()
{
    if(bOwnsPresentationTickPrerequisite)
        if(auto* Mesh=AppliedPresentationTickMesh.Get()) Mesh->RemoveTickPrerequisiteActor(this);
    AppliedPresentationTickMesh.Reset();bOwnsPresentationTickPrerequisite=false;
}
void AMCActiveSkillAction::RefreshPresentationTickPrerequisite(USkeletalMeshComponent* Mesh)
{
    if(AppliedPresentationTickMesh.Get()!=Mesh) ReleasePresentationTickPrerequisite();
    if(!IsValid(Mesh) || !Mesh->PrimaryComponentTick.bCanEverTick || !PrimaryActorTick.bCanEverTick) return;
    const bool bAlreadyPresent=Mesh->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate(
        [this](const FTickPrerequisite& Edge) {return Edge.Get()==&PrimaryActorTick;});
    if(bAlreadyPresent) {AppliedPresentationTickMesh=Mesh;return;}
    // Mesh waits for the held prop, never the reverse. Leave external DAG edges
    // untouched and refuse an indirect Mesh -> Action -> ... -> Mesh cycle.
    if(SkillTickDependsOn(PrimaryActorTick,Mesh->PrimaryComponentTick)) {
        if(!bReportedPresentationTickCycle) {
            UE_LOG(LogTemp,Warning,TEXT("Skill %s skipped presentation tick prerequisite: action already depends on mesh %s"),
                *GetName(),*Mesh->GetName());
            bReportedPresentationTickCycle=true;
        }
        return;
    }
    Mesh->AddTickPrerequisiteActor(this);
    AppliedPresentationTickMesh=Mesh;bOwnsPresentationTickPrerequisite=true;
}
void AMCActiveSkillAction::ApplyPresentationLease()
{
    if(!bCommitted || !bWantsPresentation || bFinished) {ReleasePresentation();return;}
    auto* H=Hero();
    if(AppliedPresentationPawn.Get()!=H) ReleaseAppliedPresentationLease();
    if(!H || !H->Status || !H->Status->IsAlive()) {ReleaseAppliedPresentationLease();return;}
    if(IsValid(H->GetActiveSkillPresentation()) && H->GetActiveSkillPresentation()!=this) {ReleaseAppliedPresentationLease();return;}
    if(AppliedPresentationPawn!=H) {
        H->SuspendOrdinaryWorkForSkill();H->SetActiveSkillPresentation(this);AppliedPresentationPawn=H;
    }
    RefreshPresentationTickPrerequisite(H->GetMesh());
}
void AMCActiveSkillAction::OnRep_Presentation() {ApplyPresentationLease();}
void AMCActiveSkillAction::StartLoop()
{
    if(!bCommitted || bFinished) return;
    if(HasAuthority()) bLoopRunning=true;
    OnRep_Loop();ForceNetUpdate();
}
void AMCActiveSkillAction::StopLoop()
{
    if(HasAuthority()) bLoopRunning=false;
    if(LoopComponent) LoopComponent->DeactivateImmediate();
    ForceNetUpdate();
}
void AMCActiveSkillAction::OnRep_Loop()
{
    if(!LoopComponent) return;
    ApplyPresentation();
    if(bCommitted && bLoopRunning && !bFinished && LoopComponent->GetAsset()) {
        ApplyVFXParameters(LoopComponent,FVector::ZeroVector,Param(TEXT("VFXRadius"),-1.f));
        LoopComponent->Activate(false);
    }
    else LoopComponent->DeactivateImmediate();
}
void AMCActiveSkillAction::PlaySkillCue(FName Stage,FVector Location,FVector Direction,float Radius)
{
    if(HasAuthority() && bCommitted && !Location.ContainsNaN() && !Direction.ContainsNaN() && FMath::IsFinite(Radius))
        MulticastSkillCue(Stage,Location,Direction.GetSafeNormal(),FMath::Clamp(Radius,-1.f,2000.f));
}
void AMCActiveSkillAction::MulticastSkillCue_Implementation(FName Stage,FVector_NetQuantize Location,FVector_NetQuantizeNormal Direction,float Radius)
{
    if(GetNetMode()==NM_DedicatedServer) return;
    if(Stage!=TEXT("Start") && Stage!=TEXT("Impact") && Stage!=TEXT("Warning") && Stage!=TEXT("End")) return;
    const auto& Asset=Stage==TEXT("Start")?StartVFX:Stage==TEXT("Impact")?ImpactVFX:Stage==TEXT("Warning")?WarningVFX:EndVFX;
    if(auto* System=Asset.LoadSynchronous()) if(auto* C=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Location,
        FVector(Direction).Rotation(),FVector::OneVector,true,false)) {
        ApplyVFXParameters(C,FVector(Direction),Radius);C->Activate(true);
    }
}
void AMCActiveSkillAction::ApplyVFXParameters(UNiagaraComponent* C,FVector Direction,float Radius,float Fill) const
{
    if(!C || !C->GetAsset()) return;
    // Exact typed contract read back from the authored NS assets; never create unknown overrides.
    const auto Variables=UNiagaraFunctionLibrary::GetAllUserParameters(C->GetAsset());
    const auto Has=[&](FName Name,const TCHAR* Type) {
        return Variables.ContainsByPredicate([&](const FNiagaraUserParameterInfo& P) {
            // UE's public introspection strips User.; component setters require the full name.
            const FName FullName(*(TEXT("User.")+P.ParameterName.ToString()));
            return (P.ParameterName==Name || FullName==Name) && P.ParameterType && P.ParameterType->GetPathName()==Type;
        });
    };
    if(Radius>0 && Has(TEXT("User.Radius"),TEXT("/Script/Niagara.NiagaraFloat"))) C->SetVariableFloat(TEXT("User.Radius"),FMath::Clamp(Radius,0.f,2000.f));
    if(!Direction.IsNearlyZero() && !Direction.ContainsNaN() && Has(TEXT("User.Direction"),TEXT("/Script/CoreUObject.Vector3f")))
        C->SetVariableVec3(TEXT("User.Direction"),Direction.GetSafeNormal());
    const float Intensity=Param(TEXT("VFXIntensity"),-1.f);
    if(Intensity>=0 && Has(TEXT("User.Intensity"),TEXT("/Script/Niagara.NiagaraFloat")))
        C->SetVariableFloat(TEXT("User.Intensity"),FMath::Clamp(Intensity,0.f,2.f));
    const float Duration=Param(TEXT("VFXDuration"),-1.f);
    if(Duration>0 && Has(TEXT("User.Duration"),TEXT("/Script/Niagara.NiagaraFloat"))) C->SetVariableFloat(TEXT("User.Duration"),FMath::Clamp(Duration,.1f,5.f));
    if(Fill>=0 && Has(TEXT("User.Fill"),TEXT("/Script/Niagara.NiagaraFloat"))) C->SetVariableFloat(TEXT("User.Fill"),FMath::Clamp(Fill,0.f,1.f));
    const FVector Color(Param(TEXT("VFXColorR"),-1),Param(TEXT("VFXColorG"),-1),Param(TEXT("VFXColorB"),-1));
    if(Color.GetMin()>=0 && Has(TEXT("User.Color"),TEXT("/Script/CoreUObject.LinearColor")))
        C->SetVariableLinearColor(TEXT("User.Color"),FLinearColor(FMath::Min(Color.X,4.),FMath::Min(Color.Y,4.),FMath::Min(Color.Z,4.),1));
}
void AMCActiveSkillAction::UpdateLoopVFX(FVector Direction,float Fill)
{
    ApplyVFXParameters(LoopComponent,Direction,Param(TEXT("VFXRadius"),-1.f),Fill);
}
void AMCActiveSkillAction::ReleaseInput() {}
bool AMCActiveSkillAction::RepeatInput() {return false;}
void AMCActiveSkillAction::Cancel(EMCActiveSkillEndReason Reason) {Finish(Reason);}
void AMCActiveSkillAction::OnFinished(EMCActiveSkillEndReason) {}
void AMCActiveSkillAction::Finish(EMCActiveSkillEndReason Reason)
{
    if(bFinished) return;
    bFinished=true;SetActorTickEnabled(false);
    if(GetWorld()) GetWorldTimerManager().ClearAllTimersForObject(this);
    OnFinished(Reason);
    StopLoop();ReleasePresentation();
    if(auto* P=Perks()) P->UnregisterActiveSkillAction(this);
    Destroy();
}
void AMCActiveSkillAction::EndPlay(const EEndPlayReason::Type Reason)
{
    if(!bFinished) {
        bFinished=true;
        if(GetWorld()) GetWorldTimerManager().ClearAllTimersForObject(this);
        OnFinished(EMCActiveSkillEndReason::WorldEnded);
        StopLoop();ReleasePresentation();
        if(auto* P=Perks()) P->UnregisterActiveSkillAction(this);
    }
    ReleaseAppliedPresentationLease();
    Super::EndPlay(Reason);
}
void AMCActiveSkillAction::Tick(float Dt)
{
    Super::Tick(Dt);
    // Runs on replicas too: a pawn/mesh replacement or newer presentation must
    // not leave the old mesh waiting on this actor until the next replication.
    if(bWantsPresentation || AppliedPresentationPawn.IsValid() || AppliedPresentationTickMesh.IsValid()) ApplyPresentationLease();
    if(!HasAuthority() || !bCommitted || bFinished) return;
    const auto* H=Hero();const auto* S=SourceState();
    if(!H || !S || S->GetPawn()!=H) Cancel(EMCActiveSkillEndReason::PawnChanged);
    else if(!H->Status || !H->Status->IsAlive()) Cancel(EMCActiveSkillEndReason::OwnerDied);
    if(!bFinished && Now()-StartedAt>=FMath::Clamp(Param(TEXT("MaxLifetime"),120.f),1.f,600.f)) {
        Cancel(EMCActiveSkillEndReason::TimedOut);
        Finish(EMCActiveSkillEndReason::TimedOut);
    }
}
bool AMCActiveSkillAction::GetAim(FVector& Origin,FVector& Direction) const
{
    const auto* H=Hero();if(!H) return false;
    const auto* PC=Cast<APlayerController>(H->GetController());
    FRotator Rotation=H->GetActorRotation();Origin=H->GetPawnViewLocation();
    if(PC) {
        Rotation=PC->GetControlRotation();
        if(PC->IsLocalController()) PC->GetPlayerViewPoint(Origin,Rotation);
    }
    Direction=Rotation.Vector().GetSafeNormal();
    return !Origin.ContainsNaN() && !Direction.ContainsNaN() && !Direction.IsNearlyZero();
}
void AMCActiveSkillAction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCActiveSkillAction,SourcePlayerState);DOREPLIFETIME(AMCActiveSkillAction,SourcePawn);
    DOREPLIFETIME(AMCActiveSkillAction,PerkID);DOREPLIFETIME(AMCActiveSkillAction,ActionID);
    DOREPLIFETIME(AMCActiveSkillAction,InputHandle);DOREPLIFETIME(AMCActiveSkillAction,bCommitted);
    DOREPLIFETIME(AMCActiveSkillAction,StartedAt);
    DOREPLIFETIME(AMCActiveSkillAction,PresentationReleasedAt);
    DOREPLIFETIME(AMCActiveSkillAction,bWantsPresentation);DOREPLIFETIME(AMCActiveSkillAction,bLoopRunning);
    DOREPLIFETIME_CONDITION(AMCActiveSkillAction,InitialParameters,COND_InitialOnly);
}
