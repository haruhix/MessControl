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
#include "GameFramework/WorldSettings.h"
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
    PrepareDefaultVFX();
    if(LoopComponent) {
        if(!bCapturedDefaultLoopTransform) {DefaultLoopTransform=LoopComponent->GetRelativeTransform();bCapturedDefaultLoopTransform=true;}
        if(LoopComponent->GetAsset()!=GetPreparedVFXSystem(TEXT("Loop"))) LoopComponent->SetAsset(GetPreparedVFXSystem(TEXT("Loop")));
        const auto* Slot=PreparedVFXSlot(TEXT("Loop"));
        LoopComponent->SetRelativeTransform(Slot?Slot->Transform*DefaultLoopTransform:DefaultLoopTransform);
    }
}

bool AMCActiveSkillAction::PrepareVFXProfile(UMCPerkVFXProfile* Profile,FString& OutError)
{
    OutError.Reset();
    if(!HasAuthority() || bCommitted || bFinished || IsTemplate())
    {OutError=TEXT("VFX profiles can only be prepared on an uncommitted action instance.");return false;}
    if(Profile && !Profile->Validate(OutError)) return false;
    TArray<FMCPerkVFXBinding> Candidate;
    for(const FName Stage:UMCPerkVFXProfile::GetSupportedStages()) {
        FMCPerkVFXBinding Binding;Binding.Stage=Stage;
        const auto* Override=Profile?Profile->Slots.Find(Stage):nullptr;
        if(Override && Override->Mode!=EMCPerkVFXSlotMode::UseDefault) Binding.Slot=*Override;
        else {
            const auto& Asset=Stage==TEXT("Start")?StartVFX:Stage==TEXT("Loop")?LoopVFX:Stage==TEXT("Impact")?ImpactVFX:
                Stage==TEXT("Warning")?WarningVFX:EndVFX;
            Binding.Slot.System=Asset;
            Binding.Slot.Mode=Asset.IsNull()?EMCPerkVFXSlotMode::Disabled:EMCPerkVFXSlotMode::UseDefault;
        }
        Candidate.Add(MoveTemp(Binding));
    }
    if(!PrepareVFXBindings(Candidate,OutError)) return false;
    VFXBindings=MoveTemp(Candidate);return true;
}

bool AMCActiveSkillAction::PrepareVFXBindings(const TArray<FMCPerkVFXBinding>& Bindings,FString& OutError)
{
    OutError.Reset();
    if(Bindings.Num()>5) {OutError=TEXT("Too many action VFX bindings.");return false;}
    TMap<FName,TObjectPtr<UNiagaraSystem>> Systems;
    TMap<FName,FMCPreparedPerkVFXComposition> Compositions;
    int32 TotalCompositionLayers=0;
    TSet<FName> Stages;
    for(const auto& Binding:Bindings) {
        if(!UMCPerkVFXProfile::IsSupportedStage(Binding.Stage) || Stages.Contains(Binding.Stage))
        {OutError=TEXT("Action VFX stages must be supported and unique.");return false;}
        Stages.Add(Binding.Stage);
        if(!UMCPerkVFXProfile::ValidateSlot(Binding.Slot,OutError)) return false;
        if(Binding.Slot.Mode==EMCPerkVFXSlotMode::Disabled) continue;
        if(Binding.Slot.Mode==EMCPerkVFXSlotMode::Composition)
        {
            const auto* Source=Binding.Slot.Composition.LoadSynchronous();
            if(!Source || !Source->Validate(OutError))
            {if(OutError.IsEmpty()) OutError=TEXT("Cannot load runtime composition.");OutError=Binding.Stage.ToString()+TEXT(": ")+OutError;return false;}
            FMCPreparedPerkVFXComposition Prepared;
            // Native class only: never retain an editor-only recipe subclass in
            // the prepared runtime snapshot or reference back to mutable authoring data.
            Prepared.Snapshot=NewObject<UMCVFXComposition>(GetTransientPackage(),NAME_None,RF_Transient);
            Prepared.Snapshot->DurationSeconds=Source->DurationSeconds;
            Prepared.Snapshot->TailSeconds=Source->TailSeconds;
            Prepared.Snapshot->Layers=Source->Layers;
            Prepared.Outer=Binding.Slot;
            Prepared.Systems.SetNum(Source->Layers.Num());
            for(int32 I=0;I<Source->Layers.Num();++I)
            {
                const auto& Layer=Source->Layers[I];if(!Layer.bEnabled) continue;
                if(++TotalCompositionLayers>128) {OutError=TEXT("An action accepts at most 128 enabled independent VFX layers across its events.");return false;}
                auto* LayerSystem=Layer.System.LoadSynchronous();
                if(!LayerSystem || !UMCVFXComposition::ValidateParameters(Layer,LayerSystem,OutError)
                    || !UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Binding.Slot),LayerSystem,OutError))
                {if(OutError.IsEmpty()) OutError=TEXT("An independent Niagara layer is unavailable.");OutError=Binding.Stage.ToString()+TEXT(" / ")+Layer.Name.ToString()+TEXT(": ")+OutError;return false;}
                Prepared.Systems[I]=LayerSystem;
            }
            Compositions.Add(Binding.Stage,MoveTemp(Prepared));continue;
        }
        if(Binding.Slot.System.IsNull()) continue;
        auto* System=Binding.Slot.System.LoadSynchronous();
        if(!System) {
            if(Binding.Slot.Mode==EMCPerkVFXSlotMode::UseDefault) continue; // Existing missing optional cues stay optional.
            OutError=FString::Printf(TEXT("Cannot prepare %s Niagara System."),*Binding.Stage.ToString());return false;
        }
        if(Binding.Slot.Mode==EMCPerkVFXSlotMode::Replace
            && !UMCVFXComposition::ValidateParameters(UMCPerkVFXProfile::AsLayer(Binding.Slot),System,OutError)) return false;
        Systems.Add(Binding.Stage,System);
    }
    PreparedVFXSystems=MoveTemp(Systems);PreparedVFXCompositions=MoveTemp(Compositions);PreparedVFXBindings=Bindings;bVFXPrepared=true;
    return true;
}

void AMCActiveSkillAction::PrepareDefaultVFX()
{
    if(bVFXPrepared) return;
    FString Error;
    if(!VFXBindings.IsEmpty()) {
        if(!PrepareVFXBindings(VFXBindings,Error))
            UE_LOG(LogTemp,Warning,TEXT("Skill %s could not prepare replicated VFX: %s"),*GetName(),*Error);
    }
    else if(HasAuthority() && !bCommitted) PrepareVFXProfile(nullptr,Error);
}

void AMCActiveSkillAction::OnRep_VFXBindings()
{
    FString Error;
    if(!PrepareVFXBindings(VFXBindings,Error)) {
        // Never play an earlier snapshot after a new invalid one arrives.
        PreparedVFXSystems.Reset();PreparedVFXCompositions.Reset();PreparedVFXBindings.Reset();bVFXPrepared=false;
        UE_LOG(LogTemp,Warning,TEXT("Skill %s rejected replicated VFX: %s"),*GetName(),*Error);
    }
    OnRep_Loop();
}

UNiagaraSystem* AMCActiveSkillAction::GetPreparedVFXSystem(FName Stage) const
{
    const auto* Found=PreparedVFXSystems.Find(Stage);return Found?Found->Get():nullptr;
}

void AMCActiveSkillAction::GetPreparedVFXSystems(FName Stage,TArray<UNiagaraSystem*>& OutSystems) const
{
    OutSystems.Reset();
    if(auto* Single=GetPreparedVFXSystem(Stage)) OutSystems.Add(Single);
    if(const auto* Composition=PreparedVFXCompositions.Find(Stage))
        for(UNiagaraSystem* System:Composition->Systems) if(System) OutSystems.AddUnique(System);
}

UMCVFXComposition* AMCActiveSkillAction::GetPreparedVFXComposition(FName Stage) const
{const auto* Found=PreparedVFXCompositions.Find(Stage);return Found?Found->Snapshot.Get():nullptr;}

FMCPerkVFXGameplayValues AMCActiveSkillAction::CompositionGameplayValues(FVector Direction,float Radius,float Fill) const
{
    FMCPerkVFXGameplayValues Values;Values.Direction=Direction;Values.Radius=Radius;Values.Fill=Fill;
    Values.Intensity=Param(TEXT("VFXIntensity"),-1.f);if(Values.Intensity>=0) Values.Intensity=FMath::Clamp(Values.Intensity,0.f,2.f);
    Values.Duration=Param(TEXT("VFXDuration"),-1.f);if(Values.Duration>0) Values.Duration=FMath::Clamp(Values.Duration,.1f,5.f);
    const FVector Color(Param(TEXT("VFXColorR"),-1),Param(TEXT("VFXColorG"),-1),Param(TEXT("VFXColorB"),-1));
    Values.bHasColor=Color.GetMin()>=0;
    if(Values.bHasColor) Values.Color=FLinearColor(FMath::Min(Color.X,4.),FMath::Min(Color.Y,4.),FMath::Min(Color.Z,4.),1);
    return Values;
}

AMCPerkVFXCompositionPlayer* AMCActiveSkillAction::PlayPreparedComposition(FName Stage,const FTransform& Frame,bool bLoop,
    const FMCPerkVFXGameplayValues& Values)
{
    const auto* Prepared=PreparedVFXCompositions.Find(Stage);
    if(!Prepared || GetNetMode()==NM_DedicatedServer || !GetWorld()) return nullptr;
    CompositionPlayers.RemoveAll([](const auto& Weak){return !Weak.IsValid() || !Weak->IsRunning();});
    int32 LiveBudget=0;
    for(const auto& Weak:CompositionPlayers) if(const auto* Player=Weak.Get()) LiveBudget+=Player->GetLiveLayerCount()+Player->GetPendingLayerCount();
    int32 Requested=0;for(const auto& Layer:Prepared->Snapshot->Layers) Requested+=Layer.bEnabled?1:0;
    if(CompositionPlayers.Num()>=32 || LiveBudget+Requested>128)
    {UE_LOG(LogTemp,Warning,TEXT("Skill %s rejected independent VFX event %s: explicit 32-player/128-layer action budget exceeded."),*GetName(),*Stage.ToString());return nullptr;}
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;Spawn.Owner=GetWorld()->GetWorldSettings();
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Player=GetWorld()->SpawnActor<AMCPerkVFXCompositionPlayer>(Frame.GetLocation(),Frame.Rotator(),Spawn);
    if(!Player) return nullptr;
    // Hook writes its tether/hand frames in TG_PostPhysics. The Niagara anchor
    // itself can have no active tick, so that component alone is not a source
    // action dependency. This newly spawned player has no reverse edge.
    if(bLoop) Player->AddTickPrerequisiteActor(this);
    FString Error;
    if(!Player->StartPrepared(*Prepared,Frame,Hero()?Hero()->GetMesh():nullptr,bLoop?LoopComponent.Get():nullptr,bLoop,Values,Error))
    {UE_LOG(LogTemp,Warning,TEXT("Skill %s independent VFX %s rejected: %s"),*GetName(),*Stage.ToString(),*Error);Player->Destroy();return nullptr;}
    CompositionPlayers.Add(Player);return Player;
}

void AMCActiveSkillAction::StopCompositionPlayers(bool bImmediate)
{
    for(const auto& Weak:CompositionPlayers) if(auto* Player=Weak.Get()) Player->StopEmission(bImmediate);
    CompositionPlayers.Reset();LoopCompositionPlayer.Reset();
}

const FMCPerkVFXSlot* AMCActiveSkillAction::PreparedVFXSlot(FName Stage) const
{
    const auto* Found=PreparedVFXBindings.FindByPredicate([Stage](const auto& Item){return Item.Stage==Stage;});
    return Found?&Found->Slot:nullptr;
}

void AMCActiveSkillAction::ApplyVFXSlotOverrides(UNiagaraComponent* Component,FName Stage) const
{
    const auto* Slot=PreparedVFXSlot(Stage);if(!Component || !Slot) return;
    // Typed names were validated against the system during preparation.
    for(const auto& Item:Slot->Floats) if(Item.bOverride) Component->SetVariableFloat(Item.Name,Item.Value);
    for(const auto& Item:Slot->Colors) if(Item.bOverride) Component->SetVariableLinearColor(Item.Name,Item.Value);
    for(const auto& Item:Slot->Vectors) if(Item.bOverride) Component->SetVariableVec3(Item.Name,Item.Value);
    for(const auto& Item:Slot->Bools) if(Item.bOverride) Component->SetVariableBool(Item.Name,Item.Value);
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
    if(auto* Player=LoopCompositionPlayer.Get()) Player->StopEmission(false);
    LoopCompositionPlayer.Reset();
    if(bLoopEventRunning) {bLoopEventRunning=false;OnVFXStage.Broadcast(TEXT("LoopStop"));}
    ForceNetUpdate();
}
void AMCActiveSkillAction::OnRep_Loop()
{
    if(!LoopComponent) return;
    ApplyPresentation();
    const bool bRunning=bCommitted && bLoopRunning && !bFinished;
    if(bRunning!=bLoopEventRunning) {bLoopEventRunning=bRunning;OnVFXStage.Broadcast(bRunning?TEXT("LoopStart"):TEXT("LoopStop"));}
    if(PreparedVFXCompositions.Contains(TEXT("Loop")))
    {
        LoopComponent->DeactivateImmediate();
        if(bRunning)
        {
            if(!LoopCompositionPlayer.IsValid())
            {
                if(LoopCompositionValues.Direction.IsNearlyZero()) LoopCompositionValues=CompositionGameplayValues(LoopComponent->GetForwardVector(),Param(TEXT("VFXRadius"),-1.f));
                LoopCompositionPlayer=PlayPreparedComposition(TEXT("Loop"),LoopComponent->GetComponentTransform(),true,LoopCompositionValues);
            }
        }
        else if(auto* Player=LoopCompositionPlayer.Get()) {Player->StopEmission(false);LoopCompositionPlayer.Reset();}
        return;
    }
    if(bCommitted && bLoopRunning && !bFinished && LoopComponent->GetAsset()) {
        ApplyVFXParameters(LoopComponent,FVector::ZeroVector,Param(TEXT("VFXRadius"),-1.f));
        ApplyVFXSlotOverrides(LoopComponent,TEXT("Loop"));
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
    if(Stage!=TEXT("Start") && Stage!=TEXT("Impact") && Stage!=TEXT("Warning") && Stage!=TEXT("End")) return;
    OnVFXStage.Broadcast(Stage);
    if(GetNetMode()==NM_DedicatedServer) return;
    const auto* Slot=PreparedVFXSlot(Stage);
    const FTransform EventFrame(FVector(Direction).Rotation(),FVector(Location));
    const FTransform Frame=Slot?Slot->Transform*EventFrame:EventFrame;
    if(PreparedVFXCompositions.Contains(Stage))
    {PlayPreparedComposition(Stage,Frame,false,CompositionGameplayValues(FVector(Direction),Radius));return;}
    if(auto* System=GetPreparedVFXSystem(Stage)) if(auto* C=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Frame.GetLocation(),
        Frame.Rotator(),Frame.GetScale3D(),true,false)) {
        ApplyVFXParameters(C,FVector(Direction),Radius);ApplyVFXSlotOverrides(C,Stage);C->Activate(true);
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
    ApplyVFXSlotOverrides(LoopComponent,TEXT("Loop"));
    LoopCompositionValues=CompositionGameplayValues(Direction,Param(TEXT("VFXRadius"),-1.f),Fill);
    if(auto* Player=LoopCompositionPlayer.Get()) Player->UpdateGameplayValues(LoopCompositionValues);
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
    StopLoop();
    if(Reason!=EMCActiveSkillEndReason::Completed)
        StopCompositionPlayers(Reason==EMCActiveSkillEndReason::WorldEnded || Reason==EMCActiveSkillEndReason::RunReset);
    // A completed projectile/action does not own the remaining world-space
    // impact schedule. Its prepared player retains the immutable resources.
    else CompositionPlayers.Reset();
    ReleasePresentation();
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
        StopCompositionPlayers(GetWorld() && GetWorld()->bIsTearingDown);
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
    DOREPLIFETIME_CONDITION(AMCActiveSkillAction,VFXBindings,COND_InitialOnly);
}
