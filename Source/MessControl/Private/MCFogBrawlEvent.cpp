#include "MCFogBrawlEvent.h"
#include "MCFogBrawlPresentationComponent.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "MCGripComponent.h"
#include "MCToothCharacter.h"
#include "MCToothMovementComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace MCFogBrawlEventPrivate
{
float SafeSetting(float Value,float Default,float Minimum,float Maximum)
{
    return FMath::IsFinite(Value)?FMath::Clamp(Value,Minimum,Maximum):Default;
}

void DrawPulseRing(UProceduralMeshComponent* Mesh,FVector Center,float Radius,float Width,FLinearColor Color)
{
    TArray<FVector> Vertices,Normals;
    TArray<int32> Indices;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    constexpr int32 Segments=48;
    for(int32 I=0;I<=Segments;++I)
    {
        const float Angle=I*2*PI/Segments;
        const FVector Direction(FMath::Cos(Angle),FMath::Sin(Angle),0);
        for(int32 Side=0;Side<2;++Side)
        {
            Vertices.Add(Center+Direction*(Radius+(Side?Width:-Width)));
            Normals.Add(FVector::UpVector); UV.Add(FVector2D(I/float(Segments),Side));
            Colors.Add(Color); Tangents.Add(FProcMeshTangent(Direction,false));
        }
        if(I>0)
        {
            const int32 A=(I-1)*2;
            Indices.Append({A,A+3,A+1,A,A+2,A+3});
        }
    }
    Mesh->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
}
}

AMCFogBrawlEvent::AMCFogBrawlEvent()
{
    bReplicates=true; bAlwaysRelevant=true;
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bStartWithTickEnabled=false;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    SetNetUpdateFrequency(10); SetMinNetUpdateFrequency(5);
    auto* Scene=CreateDefaultSubobject<USceneComponent>(TEXT("FogBrawlRoot")); SetRootComponent(Scene);
    SmokePresentation=CreateDefaultSubobject<UMCFogBrawlPresentationComponent>(TEXT("SmokePresentation"));
    Pulse=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("NearbyStrikePulse")); Pulse->SetupAttachment(Scene);
    Pulse->SetAbsolute(true,true,true); Pulse->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Pulse->SetCanEverAffectNavigation(false); Pulse->SetCastShadow(false); Pulse->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Colors(TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    PulseMaterial=Colors.Object; Pulse->SetMaterial(0,PulseMaterial);
    TargetGlow=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NearbyRedTooth")); TargetGlow->SetupAttachment(Scene);
    TargetGlow->SetAbsolute(true,true,true); TargetGlow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TargetGlow->SetCanEverAffectNavigation(false); TargetGlow->SetCastShadow(false); TargetGlow->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ToothColor(TEXT("/Game/Gameplay/FogBrawl/M_FogToothPulse.M_FogToothPulse"));
    ToothPulseMaterial=ToothColor.Object;
}

void AMCFogBrawlEvent::BeginPlay()
{
    Super::BeginPlay();
    if(GetNetMode()!=NM_DedicatedServer && ToothPulseMaterial) ToothPulseMID=TargetGlow->CreateDynamicMaterialInstance(0,ToothPulseMaterial);
    OnRep_State();
}

double AMCFogBrawlEvent::Now() const
{
    const auto* State=GetWorld()?GetWorld()->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;
}

bool AMCFogBrawlEvent::IsActive() const
{
    return Stage==EMCFogBrawlStage::SmokeIn || Stage==EMCFogBrawlStage::Warning || Stage==EMCFogBrawlStage::Recovery;
}

bool AMCFogBrawlEvent::IsLivingTeamMember(const AMCToothCharacter* Hero) const
{
    return IsValid(Hero) && !Hero->IsActorBeingDestroyed() && Hero->GetController() && Hero->Status && Hero->Status->IsAlive();
}

FVector AMCFogBrawlEvent::ClosestTargetPoint(const AMCToothCharacter* Hero,const AMCArenaTooth* Target) const
{
    const FVector Position=Hero?Hero->GetActorLocation():FVector::ZeroVector;
    if(!IsValid(Target) || !Target->Body) return Position;
    FVector Point;
    if(Target->Body->GetClosestPointOnCollision(Position,Point)>=0) return Point;
    // A replicated target can precede its physics state. Its authoritative box geometry is sufficient for the local tell.
    const FTransform Transform=Target->Body->GetComponentTransform();
    const FVector Local=Transform.InverseTransformPosition(Position), Extent=Target->Body->GetUnscaledBoxExtent();
    return Transform.TransformPosition(FVector(FMath::Clamp(Local.X,-Extent.X,Extent.X),
        FMath::Clamp(Local.Y,-Extent.Y,Extent.Y),FMath::Clamp(Local.Z,-Extent.Z,Extent.Z)));
}

bool AMCFogBrawlEvent::HasClearApproach(const AMCToothCharacter* Hero,const AMCArenaTooth* Target) const
{
    if(!IsValid(Hero) || !IsValid(Target) || !GetWorld()) return false;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MCFogBrawlApproach),false,Hero);
    Query.AddIgnoredActor(Target); Query.AddIgnoredActor(this);
    // Other crew members do not turn a valid defence into an obstruction.
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) Query.AddIgnoredActor(*It);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation(),ClosestTargetPoint(Hero,Target),ECC_Visibility,Query);
}

bool AMCFogBrawlEvent::IsGuarding(const AMCToothCharacter* Hero) const
{
    const auto* Movement=Hero?Hero->GetCharacterMovement():nullptr;
    return Stage==EMCFogBrawlStage::Warning && IsValid(ActiveTarget) && ActiveTarget->IsAvailable()
        && IsLivingTeamMember(Hero) && Hero->CanWork() && Hero->Grip && Hero->Grip->IsBraceInputHeld()
        && Movement && Movement->IsMovingOnGround() && !Hero->SwallowedBy && !Hero->MimicCaptor
        && !Hero->ClingTooth && !Hero->OrderJumpTarget
        && FVector::DistSquared(Hero->GetActorLocation(),ClosestTargetPoint(Hero,ActiveTarget))<=FMath::Square(GuardRadius)
        && HasClearApproach(Hero,ActiveTarget);
}

bool AMCFogBrawlEvent::ShouldRevealMarker(const AMCToothCharacter* Hero) const
{
    return Stage==EMCFogBrawlStage::Warning && IsValid(ActiveTarget) && ActiveTarget->IsAvailable()
        && IsLivingTeamMember(Hero)
        && FVector::DistSquared(Hero->GetActorLocation(),ClosestTargetPoint(Hero,ActiveTarget))<=FMath::Square(MarkerRevealDistance)
        && HasClearApproach(Hero,ActiveTarget);
}

float AMCFogBrawlEvent::WarningRemaining() const
{
    return Stage==EMCFogBrawlStage::Warning?FMath::Max(0.f,float(WarningEndsAt-Now())):0;
}

void AMCFogBrawlEvent::Start()
{
    if(!HasAuthority() || !GetWorld()) return;
    Stop(); bFailed=false;
    for(TActorIterator<AMCFogBrawlEvent> It(GetWorld());It;++It)
        if(*It!=this && It->IsActive()) { It->Stop(); It->Destroy(); }
    TotalStrikes=FMath::Clamp(TotalStrikes,1,20);
    SmokeInSeconds=MCFogBrawlEventPrivate::SafeSetting(SmokeInSeconds,3,.1f,15);
    WarningSeconds=MCFogBrawlEventPrivate::SafeSetting(WarningSeconds,4.8f,1,15);
    RecoverySeconds=MCFogBrawlEventPrivate::SafeSetting(RecoverySeconds,3,.5f,15);
    GuardRadius=MCFogBrawlEventPrivate::SafeSetting(GuardRadius,260,80,500);
    MarkerRevealDistance=MCFogBrawlEventPrivate::SafeSetting(MarkerRevealDistance,700,GuardRadius,1800);
    ReachSeconds=MCFogBrawlEventPrivate::SafeSetting(ReachSeconds,5,1,10);
    DamagePerLivingPlayer=MCFogBrawlEventPrivate::SafeSetting(DamagePerLivingPlayer,15,0,100);
    FullTeamImpulse=MCFogBrawlEventPrivate::SafeSetting(FullTeamImpulse,280,0,1400);
    MaxImpulse=MCFogBrawlEventPrivate::SafeSetting(MaxImpulse,1100,FullTeamImpulse,1400);
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    Random.Initialize((State?State->RunSeed:41)^0xf09b2026);
    StrikesResolved=0; LastGuardCount=0; LastLivingTeamCount=0;
    LastDamagePerGuard=0; LastImpulse=0; bLastToothSaved=false; LastImpactAt=-100;
    StartedAt=Now(); Stage=EMCFogBrawlStage::SmokeIn; StageEndsAt=StartedAt+SmokeInSeconds;
    SetActorTickEnabled(true); ForceNetUpdate(); OnRep_State(); PublishManualHUD();
}

void AMCFogBrawlEvent::Stop()
{
    if(!HasAuthority()) return;
    Stage=EMCFogBrawlStage::Idle; ActiveTarget=nullptr; GuardCount=0;
    WarningEndsAt=0; StageEndsAt=0; LastImpactAt=-100; bFailed=false;
    if(SmokePresentation) SmokePresentation->ResetSmoke();
    ClearPresentation(); SetActorTickEnabled(false); ForceNetUpdate();
}

void AMCFogBrawlEvent::BeginWarning()
{
    TArray<AMCArenaTooth*> Candidates;
    int32 AvailableTeeth=0;
    for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It)
    {
        if(It->IsActorBeingDestroyed() || !It->IsAvailable() || !It->Body) continue;
        ++AvailableTeeth;
        for(TActorIterator<AMCToothCharacter> Crew(GetWorld());Crew;++Crew)
        {
            if(!IsLivingTeamMember(*Crew) || !Crew->CanWork() || Crew->SwallowedBy || Crew->MimicCaptor) continue;
            const auto* Movement=Crew->GetCharacterMovement();
            const float Reach=GuardRadius+(Movement?FMath::Max(100.f,Movement->MaxWalkSpeed):250.f)*FMath::Min(ReachSeconds,WarningSeconds)*.8f;
            if(FVector::DistSquared(Crew->GetActorLocation(),ClosestTargetPoint(*Crew,*It))<=FMath::Square(Reach)
                && HasClearApproach(*Crew,*It)) { Candidates.Add(*It); break; }
        }
    }
    if(Candidates.IsEmpty())
    {
        if(AvailableTeeth==0) { Finish(true); return; }
        // Normal ragdoll/get-up, respawn and modal interactions can outlast the nominal recovery window.
        // Keep the existing reserve and give the crew time to recover or approach a reachable tooth.
        ActiveTarget=nullptr; GuardCount=0; WarningEndsAt=0;
        Stage=EMCFogBrawlStage::Recovery; StageEndsAt=Now()+.25;
        ForceNetUpdate(); OnRep_State(); return;
    }
    ActiveTarget=Candidates[Random.RandRange(0,Candidates.Num()-1)];
    Stage=EMCFogBrawlStage::Warning; WarningEndsAt=Now()+WarningSeconds; StageEndsAt=WarningEndsAt;
    TArray<AMCToothCharacter*> Guards; FindGuards(Guards); GuardCount=Guards.Num();
    ForceNetUpdate(); OnRep_State();
}

void AMCFogBrawlEvent::FindGuards(TArray<AMCToothCharacter*>& Out) const
{
    Out.Reset();
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(IsGuarding(*It)) Out.Add(*It);
}

void AMCFogBrawlEvent::ResolveStrike()
{
    if(!HasAuthority() || Stage!=EMCFogBrawlStage::Warning || !IsValid(ActiveTarget) || !ActiveTarget->IsAvailable()) return;
    TArray<AMCToothCharacter*> Guards; FindGuards(Guards);
    int32 Living=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(IsLivingTeamMember(*It)) ++Living;
    LastGuardCount=Guards.Num(); LastLivingTeamCount=Living; GuardCount=LastGuardCount;
    LastDamagePerGuard=LastGuardCount?DamagePerLivingPlayer*FMath::Max(1,Living)/LastGuardCount:0;
    LastImpulse=LastGuardCount?FMath::Min(MaxImpulse,FullTeamImpulse*FMath::Max(1,Living)/LastGuardCount):0;
    LastImpactAt=Now(); LastImpactLocation=ActiveTarget->GetActorLocation();
    bLastToothSaved=LastGuardCount>0;
    // Change stage before damage callbacks; this scheduled strike cannot resolve a second time.
    Stage=EMCFogBrawlStage::Recovery; StageEndsAt=Now()+RecoverySeconds; WarningEndsAt=0;
    ++StrikesResolved;
    if(bLastToothSaved)
    {
        for(AMCToothCharacter* Hero:Guards)
        {
            const FVector Direction=(Hero->GetActorLocation()-LastImpactLocation).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
            Hero->Grip->ReleaseBrace();
            if(auto* Movement=Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()))
            {
                Movement->CancelDash(); Movement->RestoreBraceForMove(FMCBraceMovementState());
            }
            if(Hero->Status->Damage(LastDamagePerGuard,Direction))
            {
                const FVector Velocity=Direction*LastImpulse+FVector::UpVector*70;
                if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(Velocity,Hero->GetActorLocation());
                else Hero->LaunchCharacter(Velocity,false,false);
            }
        }
    }
    else ActiveTarget->ReceiveArenaHit(ActiveTarget->State.Health,FVector(0,1,0));
    ForceNetUpdate(); OnRep_State();
}

void AMCFogBrawlEvent::Finish(bool bFailure)
{
    bFailed=bFailure; Stage=EMCFogBrawlStage::Complete; ActiveTarget=nullptr;
    GuardCount=0; WarningEndsAt=0; StageEndsAt=0;
    if(SmokePresentation) SmokePresentation->ResetSmoke();
    ClearPresentation(); SetActorTickEnabled(false); ForceNetUpdate(); PublishManualHUD();
}

void AMCFogBrawlEvent::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(HasAuthority() && IsActive())
        if(const auto* State=GetWorld()->GetGameState<AMCGameState>();State && (State->Phase==EMCShiftPhase::Won || State->Phase==EMCShiftPhase::Lost))
        { Stop(); return; }
    if(HasAuthority() && IsActive())
    {
        if(Stage==EMCFogBrawlStage::SmokeIn && Now()>=StageEndsAt) BeginWarning();
        else if(Stage==EMCFogBrawlStage::Warning)
        {
            // Reserve consumption or a concurrent normal hit invalidates this tell without spending a strike.
            if(!IsValid(ActiveTarget) || !ActiveTarget->IsAvailable()) BeginWarning();
            else
            {
                TArray<AMCToothCharacter*> Guards; FindGuards(Guards);
                if(GuardCount!=Guards.Num()) { GuardCount=Guards.Num(); ForceNetUpdate(); }
                if(Now()>=WarningEndsAt) ResolveStrike();
            }
        }
        else if(Stage==EMCFogBrawlStage::Recovery && Now()>=StageEndsAt)
        {
            if(StrikesResolved>=TotalStrikes) Finish(false); else BeginWarning();
        }
    }
    if(HasAuthority() && Stage!=EMCFogBrawlStage::Idle) PublishManualHUD();
    if(GetNetMode()!=NM_DedicatedServer) UpdatePresentation();
}

void AMCFogBrawlEvent::OnRep_State()
{
    SetActorTickEnabled(IsActive());
    if(!IsActive())
    {
        if(SmokePresentation) SmokePresentation->ResetSmoke();
        ClearPresentation(); return;
    }
    if(GetNetMode()!=NM_DedicatedServer) UpdatePresentation();
}

void AMCFogBrawlEvent::UpdatePresentation()
{
    if(!Pulse) return;
    bool bReveal=false;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(const auto* PC=It->Get();PC && PC->IsLocalController())
            if(const auto* Hero=Cast<AMCToothCharacter>(PC->GetPawn()))
            {
                if(ShouldRevealMarker(Hero)) bReveal=true;
                else if(Stage==EMCFogBrawlStage::Recovery && IsLivingTeamMember(Hero)
                    && FVector::DistSquared(Hero->GetActorLocation(),LastImpactLocation)<=FMath::Square(MarkerRevealDistance)) bReveal=true;
            }
    if(TargetGlow) TargetGlow->SetVisibility(false);
    if(!bReveal) { Pulse->SetVisibility(false); return; }
    FVector Center=LastImpactLocation;
    float Radius=GuardRadius, Width=6;
    FLinearColor Color(1,.03f,.03f);
    if(Stage==EMCFogBrawlStage::Warning && IsValid(ActiveTarget) && ActiveTarget->Body)
    {
        Center=ActiveTarget->Body->Bounds.Origin;
        Center.Z-=ActiveTarget->Body->Bounds.BoxExtent.Z;
        const float Beat=.65f+.35f*FMath::Sin(float(Now())*(WarningRemaining()<1.5f?18:8));
        Color=FLinearColor(1,.03f,.03f)*Beat;
        if(TargetGlow && ActiveTarget->Visual && ActiveTarget->Visual->GetStaticMesh())
        {
            // Own a separate local copy; normal tooth materials and their gameplay parameters stay untouched.
            TargetGlow->SetStaticMesh(ActiveTarget->Visual->GetStaticMesh());
            if(ToothPulseMID)
            {
                ToothPulseMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(3,.018f,.012f));
                ToothPulseMID->SetScalarParameterValue(TEXT("Pulse"),Beat);
                for(int32 I=0;I<TargetGlow->GetNumMaterials();++I) TargetGlow->SetMaterial(I,ToothPulseMID);
            }
            FTransform Transform=ActiveTarget->Visual->GetComponentTransform();
            Transform.SetScale3D(Transform.GetScale3D()*1.015f); TargetGlow->SetWorldTransform(Transform);
            TargetGlow->SetVisibility(ToothPulseMID!=nullptr);
        }
        Radius=FMath::Max(ActiveTarget->Body->Bounds.BoxExtent.X,ActiveTarget->Body->Bounds.BoxExtent.Y)+35;
        Width=8+5*Beat;
    }
    else
    {
        const float Age=float(Now()-LastImpactAt);
        if(Age<0 || Age>.75f) { Pulse->SetVisibility(false); return; }
        Radius=GuardRadius*(.3f+Age); Width=9;
        Color=bLastToothSaved?FLinearColor(.15f,1,.7f):FLinearColor(1,.08f,.02f);
        if(IsValid(ActiveTarget) && ActiveTarget->Body) Center.Z=ActiveTarget->Body->Bounds.Origin.Z-ActiveTarget->Body->Bounds.BoxExtent.Z;
    }
    Center.Z+=5;
    Pulse->SetWorldTransform(FTransform::Identity); MCFogBrawlEventPrivate::DrawPulseRing(Pulse,Center,Radius,Width,Color);
    Pulse->SetVisibility(true);
}

void AMCFogBrawlEvent::ClearPresentation()
{
    if(Pulse) { Pulse->ClearAllMeshSections(); Pulse->SetVisibility(false); }
    if(TargetGlow) TargetGlow->SetVisibility(false);
}

void AMCFogBrawlEvent::PublishManualHUD()
{
    auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(!HasAuthority() || !State || !State->bDevManualEvents || !ActorHasTag(TEXT("MC_DevKeyEvent"))) return;
    auto& Status=State->DirectorState;
    const FString Title=bFailed?TEXT("ДРАКА НЕ ЗАПУСТИЛАСЬ"):IsComplete()?TEXT("ДРАКА ПЕРЕЖИТА"):TEXT("ТУМАННАЯ ДРАКА");
    const FString Instruction=bFailed?TEXT("Не осталось доступных зубов для защиты. Выбери следующее событие в F3."):
        IsComplete()?TEXT("Туман рассеялся. Все удары пережиты. Выбери следующее событие в F3."):
        Stage==EMCFogBrawlStage::SmokeIn?TEXT("Рот заволакивает туман. Держитесь рядом и ищите красный мигающий зуб."):
        Stage==EMCFogBrawlStage::Recovery?TEXT("Восстановись после удара. Приготовься найти следующий мигающий зуб."):
        TEXT("Найди красный мигающий зуб и удерживай ПКМ рядом до удара. Вместе защитить его легче.");
    const int32 Total=FMath::Max(1,TotalStrikes),Left=FMath::Max(0,Total-StrikesResolved);
    if(Status.bEnabled && Status.CurrentTitle==Title && Status.Instruction==Instruction && State->TasksLeft==Left && State->TasksTotal==Total) return;
    Status.bEnabled=true; Status.CurrentTitle=Title; Status.Instruction=Instruction; Status.NextTitle.Empty();
    Status.SpawnedFood=Total; Status.FinishedFood=FMath::Clamp(StrikesResolved,0,Total);
    Status.CompletionProgress=float(Status.FinishedFood)/Total;
    State->TasksLeft=Left; State->TasksTotal=Total; State->ForceNetUpdate();
}

void AMCFogBrawlEvent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(SmokePresentation) SmokePresentation->ResetSmoke();
    ClearPresentation(); Super::EndPlay(Reason);
}

void AMCFogBrawlEvent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCFogBrawlEvent,Stage); DOREPLIFETIME(AMCFogBrawlEvent,ActiveTarget);
    DOREPLIFETIME(AMCFogBrawlEvent,StartedAt); DOREPLIFETIME(AMCFogBrawlEvent,StageEndsAt);
    DOREPLIFETIME(AMCFogBrawlEvent,WarningEndsAt); DOREPLIFETIME(AMCFogBrawlEvent,StrikesResolved);
    DOREPLIFETIME(AMCFogBrawlEvent,TotalStrikes); DOREPLIFETIME(AMCFogBrawlEvent,GuardCount);
    DOREPLIFETIME(AMCFogBrawlEvent,LastGuardCount); DOREPLIFETIME(AMCFogBrawlEvent,LastLivingTeamCount);
    DOREPLIFETIME(AMCFogBrawlEvent,LastDamagePerGuard); DOREPLIFETIME(AMCFogBrawlEvent,LastImpulse);
    DOREPLIFETIME(AMCFogBrawlEvent,bLastToothSaved); DOREPLIFETIME(AMCFogBrawlEvent,bFailed);
    DOREPLIFETIME(AMCFogBrawlEvent,LastImpactAt); DOREPLIFETIME(AMCFogBrawlEvent,LastImpactLocation);
    DOREPLIFETIME(AMCFogBrawlEvent,SmokeInSeconds); DOREPLIFETIME(AMCFogBrawlEvent,WarningSeconds);
    DOREPLIFETIME(AMCFogBrawlEvent,RecoverySeconds); DOREPLIFETIME(AMCFogBrawlEvent,GuardRadius);
    DOREPLIFETIME(AMCFogBrawlEvent,MarkerRevealDistance); DOREPLIFETIME(AMCFogBrawlEvent,ReachSeconds);
    DOREPLIFETIME(AMCFogBrawlEvent,DamagePerLivingPlayer); DOREPLIFETIME(AMCFogBrawlEvent,FullTeamImpulse);
    DOREPLIFETIME(AMCFogBrawlEvent,MaxImpulse);
}
