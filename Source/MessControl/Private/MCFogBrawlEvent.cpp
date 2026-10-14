#include "MCFogBrawlEvent.h"
#include "MCPerkComponent.h"
#include "MCFogBrawlPresentationComponent.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "MCGameMode.h"
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

void DrawPulseRing(UProceduralMeshComponent* Mesh,FVector Center,float Radius,float Width,FLinearColor Color,int32 Section=0)
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
    Mesh->CreateMeshSection_LinearColor(Section,Vertices,Indices,Normals,UV,Colors,Tangents,false);
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
    return Stage==EMCFogBrawlStage::SmokeIn || Stage==EMCFogBrawlStage::Selection
        || Stage==EMCFogBrawlStage::Warning || Stage==EMCFogBrawlStage::Recovery;
}

EMCFogBrawlSide AMCFogBrawlEvent::ToothSide(const AMCArenaTooth* Tooth)
{
    // Authored socket IDs alternate L/R; this also works when the mouth is rotated.
    return Tooth && (Tooth->State.ToothId>0?Tooth->State.ToothId%2==0:Tooth->GetActorLocation().Y>=0)
        ?EMCFogBrawlSide::Right:EMCFogBrawlSide::Left;
}

int32 AMCFogBrawlEvent::ParticipantCount() const
{
    int32 Count=0;
    for(FConstControllerIterator It=GetWorld()->GetControllerIterator();It;++It)
        if(AMCGameMode::IsGameplayParticipant(It->Get())) ++Count;
    return FMath::Clamp(Count,0,4);
}

bool AMCFogBrawlEvent::HasReadyCrew() const
{
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if(IsLivingTeamMember(*It) && It->CanWork() && !It->SwallowedBy && !It->MimicCaptor) return true;
    return false;
}

void AMCFogBrawlEvent::CollectAvailable(TArray<AMCArenaTooth*>& Out) const
{
    Out.Reset();
    for(TActorIterator<AMCArenaTooth> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed() && It->IsAvailable() && It->Body) Out.Add(*It);
    Out.Sort([](const AMCArenaTooth& A,const AMCArenaTooth& B) {
        const auto SideA=ToothSide(&A),SideB=ToothSide(&B);
        if(SideA!=SideB) return SideA==EMCFogBrawlSide::Right;
        return SideA==EMCFogBrawlSide::Right?A.State.ToothId<B.State.ToothId:A.State.ToothId>B.State.ToothId;
    });
}

float AMCFogBrawlEvent::SelectionProgress() const
{
    return Stage==EMCFogBrawlStage::Selection?FMath::Clamp(1.f-float(StageEndsAt-Now())/FMath::Max(2.f,SelectionSeconds),0.f,1.f):0;
}

int32 AMCFogBrawlEvent::SelectionStep() const
{
    const float P=SelectionProgress();
    // Four complete laps, with a tenfold increase in speed. Every reserve gets its turn.
    return FMath::FloorToInt(SelectionTeeth.Num()*(P+3*P*P*P));
}

AMCArenaTooth* AMCFogBrawlEvent::SelectionTooth() const
{
    if(Stage!=EMCFogBrawlStage::Selection || SelectionTeeth.IsEmpty()) return nullptr;
    const int32 Start=SelectionStep()%SelectionTeeth.Num();
    for(int32 I=0;I<SelectionTeeth.Num();++I) {
        auto* Tooth=SelectionTeeth[(Start+I)%SelectionTeeth.Num()].Get();
        if(IsValid(Tooth) && Tooth->IsAvailable()) return Tooth;
    }
    return nullptr;
}

bool AMCFogBrawlEvent::IsWaveTarget(const AMCArenaTooth* Tooth) const
{
    if(Stage!=EMCFogBrawlStage::Warning || !IsValid(Tooth) || !Tooth->IsAvailable()) return false;
    for(const auto& Target:Targets) if(Target.Tooth==Tooth) return !Target.bResolved && Target.Side==CurrentSide;
    return false;
}

int32 AMCFogBrawlEvent::CurrentTargetCount() const
{
    int32 Count=0;
    for(const auto& Target:Targets) if(!Target.bResolved && Target.Side==CurrentSide && IsValid(Target.Tooth) && Target.Tooth->IsAvailable()) ++Count;
    return Count;
}

AMCArenaTooth* AMCFogBrawlEvent::NearestTarget(const AMCToothCharacter* Hero,bool bCurrentSideOnly) const
{
    if(!IsValid(Hero)) return nullptr;
    AMCArenaTooth* Best=nullptr; double Distance=MAX_dbl;
    for(const auto& Target:Targets) {
        if(Target.bResolved || (bCurrentSideOnly && Target.Side!=CurrentSide) || !IsValid(Target.Tooth) || !Target.Tooth->IsAvailable()) continue;
        const double D=FVector::DistSquared(Hero->GetActorLocation(),ClosestTargetPoint(Hero,Target.Tooth));
        if(D<Distance) {Best=Target.Tooth;Distance=D;}
    }
    return Best;
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
    return GuardTarget(Hero)!=nullptr;
}

AMCArenaTooth* AMCFogBrawlEvent::GuardTarget(const AMCToothCharacter* Hero) const
{
    const auto* Movement=Hero?Hero->GetCharacterMovement():nullptr;
    if(Stage!=EMCFogBrawlStage::Warning || !IsLivingTeamMember(Hero) || !Hero->CanWork() || !Hero->Grip || !Hero->Grip->IsBraceInputHeld()
        || !Movement || !Movement->IsMovingOnGround() || Hero->SwallowedBy || Hero->MimicCaptor || Hero->ClingTooth || Hero->OrderJumpTarget) return nullptr;
    AMCArenaTooth* Best=nullptr; double Distance=FMath::Square(GuardRadius);
    for(const auto& Target:Targets) {
        if(!IsWaveTarget(Target.Tooth)) continue;
        const double D=FVector::DistSquared(Hero->GetActorLocation(),ClosestTargetPoint(Hero,Target.Tooth));
        if(D<=Distance && HasClearApproach(Hero,Target.Tooth)) {Best=Target.Tooth;Distance=D;}
    }
    // A worker protects one tooth, even if two support boxes have overlapping guard radii.
    return Best;
}

bool AMCFogBrawlEvent::IsGuardingTooth(const AMCToothCharacter* Hero,const AMCArenaTooth* Tooth) const
{
    return IsValid(Tooth) && GuardTarget(Hero)==Tooth;
}

bool AMCFogBrawlEvent::ShouldRevealMarker(const AMCToothCharacter* Hero) const
{
    return (Stage==EMCFogBrawlStage::Warning || Stage==EMCFogBrawlStage::Recovery)
        && IsLivingTeamMember(Hero) && NearestTarget(Hero,false)!=nullptr;
}

bool AMCFogBrawlEvent::IsFinalWarning() const
{
    return Stage==EMCFogBrawlStage::Warning && WarningRemaining()<=WarningSeconds;
}

bool AMCFogBrawlEvent::CanMarkTarget(const AMCToothCharacter* Hero) const
{
    return MarkableTarget(Hero)!=nullptr;
}

AMCArenaTooth* AMCFogBrawlEvent::MarkableTarget(const AMCToothCharacter* Hero) const
{
    if(Stage!=EMCFogBrawlStage::Warning || WarningRemaining()<=0 || !IsLivingTeamMember(Hero) || !Hero->CanWork()
        || Hero->SwallowedBy || Hero->MimicCaptor) return nullptr;
    AMCArenaTooth* Best=nullptr; double Distance=FMath::Square(GuardRadius);
    for(const auto& Target:Targets) {
        if(Target.bResolved || Target.bMarked || !IsValid(Target.Tooth) || !Target.Tooth->IsAvailable()) continue;
        const double D=FVector::DistSquared(Hero->GetActorLocation(),ClosestTargetPoint(Hero,Target.Tooth));
        if(D<=Distance && HasClearApproach(Hero,Target.Tooth)) {Best=Target.Tooth;Distance=D;}
    }
    return Best;
}

bool AMCFogBrawlEvent::MarkTarget(AMCToothCharacter* Hero,AMCArenaTooth* ExpectedTarget)
{
    if(!HasAuthority() || !IsValid(ExpectedTarget) || ExpectedTarget!=MarkableTarget(Hero)) return false;
    for(auto& Target:Targets) if(Target.Tooth==ExpectedTarget) {
        Target.bMarked=true; bTargetMarked=true; ForceNetUpdate(); OnRep_State(); return true;
    }
    return false;
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
    SearchSeconds=MCFogBrawlEventPrivate::SafeSetting(SearchSeconds,2.f,.1f,15);
    SelectionSeconds=MCFogBrawlEventPrivate::SafeSetting(SelectionSeconds,7,2,15);
    WarningSeconds=MCFogBrawlEventPrivate::SafeSetting(WarningSeconds,1.5f,.1f,5);
    RecoverySeconds=MCFogBrawlEventPrivate::SafeSetting(RecoverySeconds,3,.5f,15);
    GuardRadius=MCFogBrawlEventPrivate::SafeSetting(GuardRadius,260,80,500);
    MarkerRevealDistance=MCFogBrawlEventPrivate::SafeSetting(MarkerRevealDistance,700,GuardRadius,1800);
    ReachSeconds=MCFogBrawlEventPrivate::SafeSetting(ReachSeconds,7,1,15);
    StrikeDirection=StrikeDirection.GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector(0,1,0));
    DamagePerLivingPlayer=MCFogBrawlEventPrivate::SafeSetting(DamagePerLivingPlayer,15,0,100);
    FullTeamImpulse=MCFogBrawlEventPrivate::SafeSetting(FullTeamImpulse,280,0,1400);
    MaxImpulse=MCFogBrawlEventPrivate::SafeSetting(MaxImpulse,1100,FullTeamImpulse,1400);
    const auto* State=GetWorld()->GetGameState<AMCGameState>();
    Random.Initialize((State?State->RunSeed:41)^0xf09b2026);
    StrikesResolved=0; LastGuardCount=0; LastLivingTeamCount=0;
    Targets.Reset(); SelectionTeeth.Reset(); RoundPlayerCount=0;
    LastDamagePerGuard=0; LastImpulse=0; bLastToothSaved=false; LastImpactAt=-100;
    StartedAt=Now(); Stage=EMCFogBrawlStage::SmokeIn; StageEndsAt=StartedAt+SmokeInSeconds;
    SetActorTickEnabled(true); ForceNetUpdate(); OnRep_State(); PublishManualHUD();
}

void AMCFogBrawlEvent::Stop()
{
    if(!HasAuthority()) return;
    Stage=EMCFogBrawlStage::Idle; ActiveTarget=nullptr; bTargetMarked=false; GuardCount=0;
    Targets.Reset(); SelectionTeeth.Reset(); RoundPlayerCount=0;
    WarningEndsAt=0; StageEndsAt=0; LastImpactAt=-100; bFailed=false;
    if(SmokePresentation) SmokePresentation->ResetSmoke();
    ClearPresentation(); SetActorTickEnabled(false); ForceNetUpdate();
}

void AMCFogBrawlEvent::BeginSelection()
{
    if(StrikesResolved>=TotalStrikes) {Finish(false);return;}
    if(!HasReadyCrew()) {
        ActiveTarget=nullptr; GuardCount=0; WarningEndsAt=0;
        Stage=EMCFogBrawlStage::Recovery; StageEndsAt=Now()+.25;
        ForceNetUpdate(); OnRep_State(); return;
    }
    TArray<AMCArenaTooth*> Available; CollectAvailable(Available);
    if(Available.IsEmpty()) {Finish(true);return;}
    SelectionTeeth.Reset();for(auto* Tooth:Available) SelectionTeeth.Add(Tooth);
    Targets.Reset();ActiveTarget=nullptr;RoundPlayerCount=0;bTargetMarked=false;GuardCount=0;WarningEndsAt=0;
    Stage=EMCFogBrawlStage::Selection;StageEndsAt=Now()+SelectionSeconds;
    ForceNetUpdate();OnRep_State();
}

void AMCFogBrawlEvent::SelectTargets()
{
    if(StrikesResolved>=TotalStrikes) {Finish(false);return;}
    TArray<AMCArenaTooth*> Available,Right,Left;CollectAvailable(Available);
    RoundPlayerCount=ParticipantCount();
    if(RoundPlayerCount<=0 || Available.Num()<RoundPlayerCount) {Finish(true);return;}
    for(auto* Tooth:Available) (ToothSide(Tooth)==EMCFogBrawlSide::Right?Right:Left).Add(Tooth);
    Targets.Reset();
    auto Pick=[&](TArray<AMCArenaTooth*>& Pool,int32 Count) {
        for(int32 I=0;I<Count;++I) {
            const int32 Index=Random.RandRange(0,Pool.Num()-1);
            FMCFogBrawlTarget Target;Target.Tooth=Pool[Index];Target.Side=ToothSide(Target.Tooth);Targets.Add(Target);
            Pool.RemoveAtSwap(Index);
        }
    };
    if(RoundPlayerCount==1) Pick(Available,1);
    else if(TotalStrikes-StrikesResolved==1) {
        // The odd final strike must still cover one tooth per player, without
        // announcing a waiting side that would require a sixth impact.
        if(Right.Num()>=RoundPlayerCount) Pick(Right,RoundPlayerCount);
        else if(Left.Num()>=RoundPlayerCount) Pick(Left,RoundPlayerCount);
        else {Finish(true);return;}
    }
    else {
        const int32 MinimumRight=FMath::Max(1,RoundPlayerCount-Left.Num());
        const int32 MaximumRight=FMath::Min(RoundPlayerCount-1,Right.Num());
        if(MinimumRight>MaximumRight) {Finish(true);return;}
        const int32 RightCount=Random.RandRange(MinimumRight,MaximumRight);
        Pick(Right,RightCount);Pick(Left,RoundPlayerCount-RightCount);
    }
    CurrentSide=Targets.ContainsByPredicate([](const FMCFogBrawlTarget& T){return T.Side==EMCFogBrawlSide::Right;})
        ?EMCFogBrawlSide::Right:EMCFogBrawlSide::Left;
    BeginWarning();
}

bool AMCFogBrawlEvent::RefreshTargets()
{
    TArray<AMCArenaTooth*> Available;CollectAvailable(Available);
    bool Changed=false,CurrentChanged=false;
    for(auto& Target:Targets) {
        if(Target.bResolved || (IsValid(Target.Tooth) && Target.Tooth->IsAvailable())) continue;
        TArray<AMCArenaTooth*> Replacements;
        for(auto* Tooth:Available)
            if((RoundPlayerCount==1 || ToothSide(Tooth)==Target.Side)
                && !Targets.ContainsByPredicate([&](const FMCFogBrawlTarget& T){return T.Tooth==Tooth;})) Replacements.Add(Tooth);
        if(Replacements.IsEmpty()) {Finish(true);return false;}
        Target.Tooth=Replacements[Random.RandRange(0,Replacements.Num()-1)];Target.bMarked=false;
        // Solo replacement may switch sides; larger teams retain the two-side plan.
        Target.Side=ToothSide(Target.Tooth);
        if(RoundPlayerCount==1) CurrentSide=Target.Side;
        CurrentChanged|=Target.Side==CurrentSide;Changed=true;
    }
    if(Changed) {
        if(CurrentChanged && Stage==EMCFogBrawlStage::Warning) WarningEndsAt=StageEndsAt=Now()+SearchSeconds+WarningSeconds;
        ActiveTarget=nullptr;
        for(const auto& Target:Targets) if(!Target.bResolved && Target.Side==CurrentSide) {ActiveTarget=Target.Tooth;break;}
        ForceNetUpdate();OnRep_State();
    }
    return true;
}

void AMCFogBrawlEvent::BeginWarning()
{
    if(!HasReadyCrew()) {
        Stage=EMCFogBrawlStage::Recovery;StageEndsAt=Now()+.25;WarningEndsAt=0;GuardCount=0;
        ForceNetUpdate();OnRep_State();return;
    }
    if(!RefreshTargets()) return;
    ActiveTarget=nullptr;bTargetMarked=false;
    for(const auto& Target:Targets) if(!Target.bResolved && Target.Side==CurrentSide) {
        if(!ActiveTarget) ActiveTarget=Target.Tooth;
        bTargetMarked|=Target.bMarked;
    }
    if(!ActiveTarget) {Finish(true);return;}
    StrikeDirection=CurrentSide==EMCFogBrawlSide::Right?FVector(0,-1,0):FVector(0,1,0);
    Stage=EMCFogBrawlStage::Warning; WarningEndsAt=Now()+SearchSeconds+WarningSeconds; StageEndsAt=WarningEndsAt;
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
    if(!HasAuthority() || Stage!=EMCFogBrawlStage::Warning) return;
    if(StrikesResolved>=TotalStrikes) {Finish(false);return;}
    if(!RefreshTargets() || !ActiveTarget) return;
    int32 Living=0;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(IsLivingTeamMember(*It)) ++Living;
    struct FDefence {AMCToothCharacter* Hero;AMCArenaTooth* Tooth;float Damage,Impulse;};
    TArray<FDefence> Defences;TArray<AMCArenaTooth*> Missed;
    LastGuardCount=0;LastLivingTeamCount=Living;LastDamagePerGuard=0;LastImpulse=0;bLastToothSaved=true;
    const float Share=float(FMath::Max(1,Living))/FMath::Max(1,Targets.Num());
    // Snapshot all guards before recoil or damage changes anyone's eligibility.
    for(auto& Target:Targets) {
        if(!IsWaveTarget(Target.Tooth)) continue;
        TArray<AMCToothCharacter*> Guards;
        for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(IsGuardingTooth(*It,Target.Tooth)) Guards.Add(*It);
        Target.Guards=Guards.Num();Target.bSaved=!Guards.IsEmpty();Target.ImpactAt=Now();Target.bMarked=false;
        LastGuardCount+=Guards.Num();bLastToothSaved&=Target.bSaved;
        if(Guards.IsEmpty()) Missed.Add(Target.Tooth);
        else for(auto* Hero:Guards) {
            const float Damage=DamagePerLivingPlayer*Share/Guards.Num();
            const float Impulse=FMath::Min(MaxImpulse,FullTeamImpulse*Share/Guards.Num());
            Defences.Add({Hero,Target.Tooth,Damage,Impulse});LastDamagePerGuard+=Damage;LastImpulse=FMath::Max(LastImpulse,Impulse);
        }
    }
    if(LastGuardCount) LastDamagePerGuard/=LastGuardCount;
    GuardCount=LastGuardCount;
    LastImpactAt=Now(); LastImpactLocation=ActiveTarget->GetActorLocation();
    // Change stage before damage callbacks; this scheduled strike cannot resolve a second time.
    Stage=EMCFogBrawlStage::Recovery; StageEndsAt=Now()+RecoverySeconds; WarningEndsAt=0; bTargetMarked=false;
    for(auto& Target:Targets) if(!Target.bResolved && Target.Side==CurrentSide) Target.bResolved=true;
    ++StrikesResolved;
    const bool Pending=Targets.ContainsByPredicate([](const FMCFogBrawlTarget& T){return !T.bResolved;});
    if(StrikesResolved>=TotalStrikes)
        Targets.RemoveAll([](const FMCFogBrawlTarget& T){return !T.bResolved;});
    else if(Pending) CurrentSide=EMCFogBrawlSide::Left;
    for(const auto& Defence:Defences)
    {
            auto* Hero=Defence.Hero;
            const FVector Direction=(Hero->GetActorLocation()-Defence.Tooth->GetActorLocation()).GetSafeNormal2D(KINDA_SMALL_NUMBER,FVector::ForwardVector);
            Hero->Grip->ReleaseBrace();
            if(auto* Movement=Cast<UMCToothMovementComponent>(Hero->GetCharacterMovement()))
            {
                Movement->CancelDash(); Movement->RestoreBraceForMove(FMCBraceMovementState());
            }
            if(Hero->Status->Damage(Defence.Damage,Direction,this,EMCRogueDamageKind::Brace))
            {
                const FVector Velocity=Direction*Defence.Impulse+FVector::UpVector*70;
                if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(Velocity,Hero->GetActorLocation());
                else Hero->LaunchCharacter(Velocity,false,false);
            }
    }
    // Three unguarded strikes against an intact tooth: chip, broken crown, then normal loss.
    for(auto* Tooth:Missed) Tooth->ReceiveArenaHit(Tooth->State.Health<=Tooth->Settings.MaxHealth/3.f+.01f
        ?Tooth->State.Health:Tooth->Settings.MaxHealth/3.f,StrikeDirection);
    ForceNetUpdate(); OnRep_State();
}

void AMCFogBrawlEvent::Finish(bool bFailure)
{
    bFailed=bFailure; Stage=EMCFogBrawlStage::Complete; ActiveTarget=nullptr; bTargetMarked=false;
    Targets.Reset();SelectionTeeth.Reset();
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
        if(Stage==EMCFogBrawlStage::SmokeIn && Now()>=StageEndsAt) BeginSelection();
        else if(Stage==EMCFogBrawlStage::Selection && Now()>=StageEndsAt) SelectTargets();
        else if(Stage==EMCFogBrawlStage::Warning)
        {
            // Reserve consumption or a concurrent normal hit invalidates this tell without spending a strike.
            if(RefreshTargets() && Stage==EMCFogBrawlStage::Warning)
            {
                TArray<AMCToothCharacter*> Guards; FindGuards(Guards);
                if(GuardCount!=Guards.Num()) { GuardCount=Guards.Num(); ForceNetUpdate(); }
                if(Now()>=WarningEndsAt) ResolveStrike();
            }
        }
        else if(Stage==EMCFogBrawlStage::Recovery && Now()>=StageEndsAt)
        {
            if(StrikesResolved>=TotalStrikes) Finish(false);
            else if(Targets.ContainsByPredicate([](const FMCFogBrawlTarget& T){return !T.bResolved;})) BeginWarning();
            else BeginSelection();
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
    Pulse->ClearAllMeshSections();Pulse->SetVisibility(false);
    if(TargetGlow) TargetGlow->SetVisibility(false);
    for(auto& Pair:ToothGlows) if(Pair.Value) Pair.Value->SetVisibility(false);
    auto ShowTooth=[&](AMCArenaTooth* Tooth,FLinearColor Tint,float Strength) {
        if(!IsValid(Tooth) || !Tooth->IsAvailable() || !Tooth->Visual || !Tooth->Visual->GetStaticMesh() || !ToothPulseMaterial) return;
        auto& Glow=ToothGlows.FindOrAdd(Tooth);
        if(!Glow) {
            if(ToothGlows.Num()==1) Glow=TargetGlow;
            else {
                Glow=NewObject<UStaticMeshComponent>(this);
                Glow->SetupAttachment(GetRootComponent());Glow->SetAbsolute(true,true,true);
                Glow->SetCollisionEnabled(ECollisionEnabled::NoCollision);Glow->SetCastShadow(false);
                Glow->SetCanEverAffectNavigation(false);Glow->RegisterComponent();
            }
        }
        Glow->SetStaticMesh(Tooth->Visual->GetStaticMesh());
        auto* MID=Cast<UMaterialInstanceDynamic>(Glow->GetMaterial(0));
        if(!MID || MID->Parent!=ToothPulseMaterial) MID=Glow->CreateDynamicMaterialInstance(0,ToothPulseMaterial);
        if(!MID) return;
        MID->SetVectorParameterValue(TEXT("Tint"),Tint);MID->SetScalarParameterValue(TEXT("Pulse"),Strength);
        for(int32 I=0;I<Glow->GetNumMaterials();++I) Glow->SetMaterial(I,MID);
        FTransform Transform=Tooth->Visual->GetComponentTransform();Transform.SetScale3D(Transform.GetScale3D()*1.015f);
        Glow->SetWorldTransform(Transform);Glow->SetVisibility(true);
    };
    if(Stage==EMCFogBrawlStage::Selection) {
        ShowTooth(SelectionTooth(),FLinearColor(3,1.4f,.08f),.95f);return;
    }
    int32 Section=0;
    for(const auto& Target:Targets) {
        if(!IsValid(Target.Tooth) || !Target.Tooth->Body) continue;
        const auto Bounds=Target.Tooth->Body->Bounds;
        FVector Center=Bounds.Origin+FVector(0,0,Bounds.BoxExtent.Z*.65f);
        const float Radius=FMath::Max(Bounds.BoxExtent.X,Bounds.BoxExtent.Y)+35;
        if(!Target.bResolved && (Stage==EMCFogBrawlStage::Warning || Stage==EMCFogBrawlStage::Recovery)) {
            const bool Current=IsWaveTarget(Target.Tooth);
            const float Beat=Current?.72f+.28f*FMath::Sin(float(Now())*(IsFinalWarning()?18:7)):.72f;
            const auto Color=Current?FLinearColor(3,.025f,.015f):FLinearColor(2.5f,1.35f,.06f);
            ShowTooth(Target.Tooth,Color,Beat);
            if(Current) MCFogBrawlEventPrivate::DrawPulseRing(Pulse,Center,Radius,8+5*Beat,FLinearColor(1,.03f,.02f)*Beat,Section++);
        } else if(Target.bResolved && FMath::Abs(Target.ImpactAt-LastImpactAt)<.001 && Now()-LastImpactAt<.75) {
            const float Age=FMath::Max(0.f,float(Now()-LastImpactAt));
            MCFogBrawlEventPrivate::DrawPulseRing(Pulse,Center,Radius*(1+Age*.3f),9,
                Target.bSaved?FLinearColor(.15f,1,.7f):FLinearColor(1,.08f,.02f),Section++);
        }
    }
    Pulse->SetWorldTransform(FTransform::Identity);Pulse->SetVisibility(Section>0);
}

void AMCFogBrawlEvent::ClearPresentation()
{
    if(Pulse) { Pulse->ClearAllMeshSections(); Pulse->SetVisibility(false); }
    if(TargetGlow) TargetGlow->SetVisibility(false);
    for(auto& Pair:ToothGlows) if(Pair.Value) Pair.Value->SetVisibility(false);
}

void AMCFogBrawlEvent::PublishManualHUD()
{
    auto* State=GetWorld()->GetGameState<AMCGameState>();
    if(!HasAuthority() || !State || !State->bDevManualEvents || !ActorHasTag(TEXT("MC_DevKeyEvent"))) return;
    auto& Status=State->DirectorState;
    const FString Title=bFailed?TEXT("ДРАКА НЕ ЗАПУСТИЛАСЬ"):IsComplete()?TEXT("ДРАКА ПЕРЕЖИТА"):TEXT("ТУМАННАЯ ДРАКА");
    const FString Instruction=bFailed?TEXT("Недостаточно доступных зубов для выбора по числу игроков и сторонам. Выбери следующее событие в F3."):
        IsComplete()?TEXT("Туман рассеялся. Все удары пережиты. Выбери следующее событие в F3."):
        Stage==EMCFogBrawlStage::SmokeIn?TEXT("Туман сгущается. Сейчас все доступные зубы начнут мигать по очереди."):
        Stage==EMCFogBrawlStage::Selection?TEXT("Подсветка ускоряется. После выбора распределитесь по отмеченным зубам."):
        Stage==EMCFogBrawlStage::Recovery?TEXT("Восстановись. Красные цели получают удар раньше жёлтых: справа, затем слева."):
        TEXT("Красные зубы — ближайший удар; жёлтые — следующая сторона. У выбранного зуба держи ПКМ.");
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
    DOREPLIFETIME(AMCFogBrawlEvent,bTargetMarked);
    DOREPLIFETIME(AMCFogBrawlEvent,StartedAt); DOREPLIFETIME(AMCFogBrawlEvent,StageEndsAt);
    DOREPLIFETIME(AMCFogBrawlEvent,WarningEndsAt); DOREPLIFETIME(AMCFogBrawlEvent,StrikesResolved);
    DOREPLIFETIME(AMCFogBrawlEvent,TotalStrikes); DOREPLIFETIME(AMCFogBrawlEvent,GuardCount);
    DOREPLIFETIME(AMCFogBrawlEvent,LastGuardCount); DOREPLIFETIME(AMCFogBrawlEvent,LastLivingTeamCount);
    DOREPLIFETIME(AMCFogBrawlEvent,LastDamagePerGuard); DOREPLIFETIME(AMCFogBrawlEvent,LastImpulse);
    DOREPLIFETIME(AMCFogBrawlEvent,bLastToothSaved); DOREPLIFETIME(AMCFogBrawlEvent,bFailed);
    DOREPLIFETIME(AMCFogBrawlEvent,LastImpactAt); DOREPLIFETIME(AMCFogBrawlEvent,LastImpactLocation);
    DOREPLIFETIME(AMCFogBrawlEvent,SmokeInSeconds); DOREPLIFETIME(AMCFogBrawlEvent,WarningSeconds);
    DOREPLIFETIME(AMCFogBrawlEvent,SearchSeconds); DOREPLIFETIME(AMCFogBrawlEvent,StrikeDirection);
    DOREPLIFETIME(AMCFogBrawlEvent,SelectionSeconds);DOREPLIFETIME(AMCFogBrawlEvent,SelectionTeeth);
    DOREPLIFETIME(AMCFogBrawlEvent,Targets);DOREPLIFETIME(AMCFogBrawlEvent,CurrentSide);DOREPLIFETIME(AMCFogBrawlEvent,RoundPlayerCount);
    DOREPLIFETIME(AMCFogBrawlEvent,RecoverySeconds); DOREPLIFETIME(AMCFogBrawlEvent,GuardRadius);
    DOREPLIFETIME(AMCFogBrawlEvent,MarkerRevealDistance); DOREPLIFETIME(AMCFogBrawlEvent,ReachSeconds);
    DOREPLIFETIME(AMCFogBrawlEvent,DamagePerLivingPlayer); DOREPLIFETIME(AMCFogBrawlEvent,FullTeamImpulse);
    DOREPLIFETIME(AMCFogBrawlEvent,MaxImpulse);
}
