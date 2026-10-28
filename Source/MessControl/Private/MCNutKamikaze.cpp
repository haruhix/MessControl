#include "MCNutKamikaze.h"
#include "MCCombatPassiveSubsystem.h"

#include "MCGameState.h"
#include "MCNutCombatEffect.h"
#include "MCNutCombatVisuals.h"
#include "MCCombatFeedbackComponent.h"
#include "MCWeaponImpact.h"
#include "MCNutRainEvent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

// Server-only coordination: actor references remain weak, so killing a summon
// cannot keep it alive or strand a timer on a defeated queue leader.
struct FMCNutKamikazeFuseGroup
{
    static constexpr double Interval=.75;
    TArray<TWeakObjectPtr<AMCNutKamikaze>> Members;
    int32 NextIndex=0;
    double EarliestNextBlast=-1;
};

namespace
{
float SafeValue(float Value,float Default,float Minimum,float Maximum)
{return FMath::Clamp(FMath::IsFinite(Value)?Value:Default,Minimum,Maximum);}

void RenderFuseSparks(UProceduralMeshComponent* Component,float Phase,float Progress,float Remaining,float Duration)
{
    const float EndWindow=FMath::Min(1.5f,Duration*.38f);
    const float Urgency=1-FMath::SmoothStep(0.f,EndWindow,Remaining);
    if(Urgency<=.001f) {Component->SetMeshSectionVisible(1,false);return;}
    MCNutCombatVisuals::FMesh Sparks;
    constexpr int32 Count=6;
    Sparks.Points.Reserve(Count*8);Sparks.Normals.Reserve(Count*8);Sparks.UV.Reserve(Count*8);
    Sparks.Colors.Reserve(Count*8);Sparks.Tangents.Reserve(Count*8);Sparks.Indices.Reserve(Count*12);
    for(int32 Index=0;Index<Count;++Index) {
        const float Cycle=Phase/(2*PI)*.55f+Index*.618034f;
        const float T=FMath::Frac(Cycle);
        const float Angle=Index*2.39996f+FMath::FloorToFloat(Cycle)*1.13f;
        const FVector Direction=FVector(FMath::Cos(Angle),FMath::Sin(Angle),.7f+.08f*Index).GetSafeNormal();
        const FVector Side=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal();
        const FVector Across=FVector::CrossProduct(Direction,Side).GetSafeNormal();
        const FVector Point=FVector(0,0,18+18*Progress)+Direction*((24+48*Urgency)*T)
            -FVector(0,0,18*T*T);
        const float Alpha=Urgency*FMath::Sin(T*PI)*.80f;
        const FLinearColor Color(FMath::Frac(Index*.37f),.85f,0,Alpha);
        const float Length=FMath::Lerp(2.5f,6.5f,Urgency)*(1-T*.55f);
        Sparks.Quad(Point,Side,Direction,1.1f,Length,Color);
        Sparks.Quad(Point,Across,Direction,.85f,Length,Color);
    }
    Sparks.Upload(Component,1);Component->SetMeshSectionVisible(1,true);
}
}

AMCNutKamikaze::AMCNutKamikaze()
{
    Settings.MaxHealth=40;
    Health=Settings.MaxHealth;
    SummonModel=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SummonModel"));
    SummonModel->SetupAttachment(RootComponent);
    SummonModel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SummonModel->SetCanEverAffectNavigation(false);SummonModel->SetVisibility(false);
    SummonModel->bEnableUpdateRateOptimizations=false;
    SummonMesh=FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutSummon/SK_NutSummon.SK_NutSummon"));
    RunAnimation=FSoftObjectPath(TEXT("/Game/Gameplay/CoreLoop/NutSummon/Animations/A_NutSummon_Run.A_NutSummon_Run"));
    FuseFlame=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BurningFuse"));
    FuseFlame->SetupAttachment(RootComponent);
    BlastWarning=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BlastFootprint"));
    BlastWarning->SetupAttachment(RootComponent);
    for(UProceduralMeshComponent* Part:{FuseFlame.Get(),BlastWarning.Get()}) {
        MCNutCombatVisuals::Configure(Part);
        Part->SetVisibility(false);
    }
    // The footprint stays on the live floor even as the nut turns towards a new target.
    BlastWarning->SetUsingAbsoluteRotation(true);
    BlastWarning->SetTranslucentSortPriority(4);
    FuseMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/M_ReactionFire.M_ReactionFire"));
    WarningMaterial=FSoftObjectPath(TEXT("/Game/Gameplay/VFX/NutCombat/M_NutTelegraph.M_NutTelegraph"));
}

double AMCNutKamikaze::ServerNow() const
{
    const UWorld* World=GetWorld();
    const auto* Game=World?World->GetGameState():nullptr;
    return Game?Game->GetServerWorldTimeSeconds():(World?World->GetTimeSeconds():0.);
}

void AMCNutKamikaze::BeginPlay()
{
    Super::BeginPlay();
    if(IsValid(Tongue)) AddTickPrerequisiteActor(Tongue);
    if(GetNetMode()!=NM_DedicatedServer) {
        auto* Mesh=SummonMesh.LoadSynchronous();auto* Run=RunAnimation.LoadSynchronous();
        if(Mesh && Run && Run->GetSkeleton()==Mesh->GetSkeleton()) {
            SummonModel->SetSkeletalMeshAsset(Mesh);
            SummonModel->SetAnimationMode(EAnimationMode::AnimationSingleNode);
            SummonModel->PlayAnimation(Run,true);SummonModel->SetPlayRate(0.f);
            SummonModel->AddTickPrerequisiteActor(this);
            const auto Bounds=Mesh->GetBounds();ModelCenter=Bounds.Origin;
            ModelScale=FVector(ModelHeight/FMath::Max(1.,Bounds.BoxExtent.Z*2));
            FuseFlame->AttachToComponent(SummonModel,FAttachmentTransformRules::KeepRelativeTransform,TEXT("head_x"));
            FuseFlame->SetAbsolute(false,true,true);FuseFlame->SetWorldRotation(FRotator::ZeroRotator);
            // This supplied head's local -Y/-Z diagonal points up through its crown.
            FuseFlame->SetRelativeLocation(FVector(0,-23,-25));
        }
        if(auto* Material=FuseMaterial.LoadSynchronous()) {
            FuseMID=FuseFlame->CreateDynamicMaterialInstance(0,Material);
            FuseFlame->SetMaterial(1,FuseMID);
        }
        if(auto* Material=WarningMaterial.LoadSynchronous()) WarningMID=BlastWarning->CreateDynamicMaterialInstance(0,Material);
        PresentFuse(ServerNow());
        PresentSummon(ServerNow(),0);
    }
    else SummonModel->SetComponentTickEnabled(false);
}

bool AMCNutKamikaze::ConfigureKamikaze(AMCTongue* OnTongue,UStaticMesh* Mesh,FVector Scale,FMCNutEnemySettings InSettings,
    float FuseSeconds,float InBlastRadius,float InBlastDamage,float InPushSpeed)
{
    if(!HasAuthority() || !IsValid(OnTongue) || OnTongue->GetWorld()!=GetWorld() || !IsValid(Mesh) || Scale.ContainsNaN()) return false;
    FuseGroup.Reset();
    ConfigureEnemy(OnTongue,Mesh,Scale,InSettings);
    BlastRadius=SafeValue(InBlastRadius,230.f,80.f,600.f);
    BlastDamage=SafeValue(InBlastDamage,20.f,0.f,40.f);
    PushSpeed=SafeValue(InPushSpeed,650.f,0.f,1200.f);
    FuseStartedAt=ServerNow();
    FuseExplodesAt=FuseStartedAt+SafeValue(FuseSeconds,5.f,.8f,12.f);
    bArmed=true; bExploded=false; NextNearestTargetAt=FuseStartedAt;
    // A weapon hit may change the base enemy's target. This subtype always resumes nearest-player pursuit.
    Retarget(); RefreshPresentation(); ForceNetUpdate();
    return true;
}

void AMCNutKamikaze::StaggerSummonedGroup(const TArray<AMCNutKamikaze*>& Summons)
{
    auto Group=MakeShared<FMCNutKamikazeFuseGroup>();
    UWorld* World=nullptr;
    double PreviousDeadline=-1;
    for(auto* Nut:Summons) {
        if(!IsValid(Nut) || !Nut->HasAuthority() || !Nut->bArmed || !Nut->IsEncounterAlive()
            || (World && Nut->GetWorld()!=World)
            || Group->Members.ContainsByPredicate([Nut](const auto& Member) {return Member.Get()==Nut;})) continue;
        World=Nut->GetWorld();
        if(PreviousDeadline>=0) Nut->FuseExplodesAt=FMath::Max(Nut->FuseExplodesAt,PreviousDeadline+Group->Interval);
        PreviousDeadline=Nut->FuseExplodesAt;
        Nut->FuseGroup=Group; Group->Members.Add(Nut); Nut->ForceNetUpdate();
    }
}

bool AMCNutKamikaze::IsDetonationDue(double Time)
{
    if(!FuseGroup) return Time>=FuseExplodesAt;
    while(FuseGroup->Members.IsValidIndex(FuseGroup->NextIndex)) {
        auto* Next=FuseGroup->Members[FuseGroup->NextIndex].Get();
        if(Next && Next->bArmed && Next->IsEncounterAlive() && Next->FuseGroup==FuseGroup)
            return Next==this && Time>=FuseExplodesAt && Time>=FuseGroup->EarliestNextBlast;
        ++FuseGroup->NextIndex;
    }
    return false;
}

void AMCNutKamikaze::AdvanceGroupAfterBlast(double Time)
{
    if(!FuseGroup) return;
    ++FuseGroup->NextIndex;
    FuseGroup->EarliestNextBlast=Time+FuseGroup->Interval;
    double Earliest=FuseGroup->EarliestNextBlast;
    // Rebase only after a real explosion, never every Tick. A delayed server
    // frame must not dispatch all overdue fuses together or perpetually extend them.
    for(int32 Index=FuseGroup->NextIndex;Index<FuseGroup->Members.Num();++Index) {
        auto* Nut=FuseGroup->Members[Index].Get();
        if(!Nut || !Nut->bArmed || !Nut->IsEncounterAlive() || Nut->FuseGroup!=FuseGroup) continue;
        const double Deadline=FMath::Max(Nut->FuseExplodesAt,Earliest);
        if(Deadline>Nut->FuseExplodesAt) {Nut->FuseExplodesAt=Deadline;Nut->ForceNetUpdate();}
        Earliest=Deadline+FuseGroup->Interval;
    }
}

float AMCNutKamikaze::GetFuseSecondsRemaining() const
{return bArmed && !bDefeated?FMath::Max(0.f,float(FuseExplodesAt-ServerNow())):0.f;}

void AMCNutKamikaze::RefreshPresentation()
{
    Super::RefreshPresentation();
    if(FuseFlame && BlastWarning) {
        FuseFlame->SetVisibility(bArmed && !bDefeated && FuseMID);
        BlastWarning->SetVisibility(false);
    }
    PresentSummon(ServerNow(),0);
}

void AMCNutKamikaze::PresentSummon(double Time,float Dt)
{
    if(GetNetMode()==NM_DedicatedServer || !SummonModel || !SummonModel->GetSkeletalMeshAsset()) return;
    Visual->SetVisibility(false);LeftEye->SetVisibility(false);RightEye->SetVisibility(false);
    Feedback->SetVisualTarget(SummonModel);
    // Retain the existing hit/fuse/death pulse, with feet anchored to the live
    // floor instead of adding the old static walnut's procedural hop.
    const auto Bounds=SummonModel->GetSkeletalMeshAsset()->GetBounds();
    const float Height=NutMesh?float(NutMesh->GetBounds().BoxExtent.Z*2*MeshScale.Z):ModelHeight;
    ModelScale=FVector(Height/FMath::Max(1.,Bounds.BoxExtent.Z*2));
    const FVector Scale=ModelScale*(Visual->GetRelativeScale3D()/MeshScale);
    const FRotator Rotation(0,ModelYaw,0);
    SummonModel->SetRelativeRotation(Rotation);SummonModel->SetRelativeScale3D(Scale);
    const double HalfHeight=SummonModel->GetSkeletalMeshAsset()->GetBounds().BoxExtent.Z*Scale.Z;
    const float Hit=Feedback->GetHitPulse();
    const FVector Recoil=GetActorTransform().InverseTransformVectorNoScale(Feedback->LastHit.Direction).GetSafeNormal2D()
        *(Settings.BodyRadius*.42f*Hit);
    SummonModel->SetRelativeLocation(-Rotation.RotateVector(ModelCenter*Scale)+FVector(0,0,HalfHeight-Settings.BodyRadius)+Recoil);
    SummonModel->SetVisibility(Scale.GetMax()>.001f);
    if(Dt>SMALL_NUMBER) {
        const FVector Location=GetActorLocation();
        const float Speed=bModelMotionInitialized?FVector::Dist2D(Location,LastModelLocation)/Dt:0.f;
        const float TargetRate=!bDefeated && !MCWeaponImpact::IsPoseHeld(this)?FMath::Clamp(Speed/FMath::Max(40.f,RunReferenceSpeed),0.f,4.f):0.f;
        RunPlayRate=MCWeaponImpact::IsPoseHeld(this)?0.f:FMath::FInterpTo(RunPlayRate,TargetRate,Dt,14.f);
        SummonModel->SetPlayRate(RunPlayRate);LastModelLocation=Location;bModelMotionInitialized=true;
    }
}

void AMCNutKamikaze::PresentFuse(double Time)
{
    TickPresentation(Time);
    if(!bArmed || bDefeated) {
        FuseFlame->SetVisibility(false); BlastWarning->SetVisibility(false); return;
    }
    const float Remaining=FMath::Max(0.f,float(FuseExplodesAt-Time));
    const float Duration=FMath::Max(.1f,float(FuseExplodesAt-FuseStartedAt));
    const float Progress=FMath::Clamp(1.f-Remaining/Duration,0.f,1.f);
    const float Age=FMath::Max(0.f,float(Time-FuseStartedAt));
    // Integrate the rising frequency against the absolute fuse clock. This
    // stays continuous for every authored duration, including a late client.
    const float VisualAge=FMath::Min(Age,Duration);
    const float Phase=5.f*VisualAge+21.f*Duration*Progress*Progress*Progress/3.f;
    const float Pulse=.5f+.5f*FMath::Sin(Phase);
    const float Swell=1.f+Progress*.12f+Pulse*Progress*.08f;
    Visual->SetRelativeScale3D(Visual->GetRelativeScale3D()*Swell);
    Label->SetText(FText::FromString(FString::Printf(TEXT("%d HP | %.1f s"),FMath::CeilToInt(Health),Remaining)));
    Label->SetTextRenderColor(FColor(255,FMath::RoundToInt(FMath::Lerp(190.f,45.f,Progress)),30));
    if(FuseMID) {
        if(!SummonModel->GetSkeletalMeshAsset()) FuseFlame->SetRelativeLocation(Visual->GetRelativeLocation()+FVector(0,0,Settings.BodyRadius*.8f));
        const float FireAge=Phase/5.f;
        FuseMID->SetScalarParameterValue(TEXT("FireAge"),FireAge);
        MCNutCombatVisuals::RenderFlames(FuseFlame,(7.f+Progress*5.f)*(1+Pulse*Progress*.07f),FireAge,.75f+Pulse*Progress*.17f);
        RenderFuseSparks(FuseFlame,Phase,Progress,Remaining,Duration);
        FuseFlame->SetVisibility(true);
    }
    FHitResult Floor;
    const bool ShowFootprint=Remaining<=1.5f && WarningMID && IsValid(Tongue) && Tongue->SurfacePoint(GetActorLocation(),Floor);
    BlastWarning->SetVisibility(ShowFootprint);
    if(ShowFootprint) {
        const FVector Normal=Floor.ImpactNormal.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);
        const FVector Right=FVector::VectorPlaneProject(FVector::ForwardVector,Normal).GetSafeNormal(SMALL_NUMBER,FVector::RightVector);
        const FVector Side=FVector::CrossProduct(Normal,Right).GetSafeNormal();
        MCNutCombatVisuals::FMesh Area;
        Area.Quad(FVector::ZeroVector,Right,Side,BlastRadius,BlastRadius,FLinearColor(1,1,1,.7f+Pulse*.3f));
        Area.Upload(BlastWarning);
        BlastWarning->SetWorldLocation(Floor.ImpactPoint+Normal*7.f);
        WarningMID->SetScalarParameterValue(TEXT("Age"),Age);
        WarningMID->SetScalarParameterValue(TEXT("Progress"),Progress);
        WarningMID->SetScalarParameterValue(TEXT("IsLine"),0);
        WarningMID->SetScalarParameterValue(TEXT("Impact"),0);
        WarningMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1.f,.12f,.02f));
    }
}

void AMCNutKamikaze::Detonate()
{
    if(!HasAuthority() || !bArmed || !IsEncounterAlive()) return;
    bArmed=false; bExploded=true;
    const FVector Center=GetActorLocation();
    if(auto* Effect=AMCNutCombatEffect::Spawn(this,Tongue,EMCNutCombatCue::FireImpact,Center+FVector::UpVector*30.f,Center,
        BlastRadius,0,.65f,GetUniqueID(),Settings.BodyRadius)) {
        if(auto* Event=Cast<AMCNutRainEvent>(GetOwner())) {
            Effect->SetOwner(Event); Event->TrackEncounterActor(Effect);
        }
    }
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) {
        AMCToothCharacter* Hero=*It;
        if(!IsLiveTarget(Hero) || FVector::DistSquared2D(Center,Hero->GetActorLocation())>FMath::Square(BlastRadius)
            || FMath::Abs(Hero->GetActorLocation().Z-Center.Z)>230.f) continue;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCNutKamikazeBlast),false,this); Query.AddIgnoredActor(Hero);
        if(GetWorld()->LineTraceSingleByChannel(Hit,Center,Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        const FVector Direction=(Hero->GetActorLocation()-Center).GetSafeNormal2D(SMALL_NUMBER,GetActorForwardVector());
        if(BlastDamage>0.f && !Hero->Status->Damage(BlastDamage,Direction,this)) continue;
        const FVector Impulse=Direction*PushSpeed+FVector::UpVector*90.f;
        if(PushSpeed>0.f) {
            if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(Impulse,Hero->GetActorLocation());
            else Hero->LaunchCharacter(Impulse,false,false);
        }
    }
    HitAt=ServerNow();
    AdvanceGroupAfterBlast(HitAt);
    Defeat();
}

void AMCNutKamikaze::Defeat()
{
    // Tool/booster/chili kills use the same base damage resolver and permanently disarm the fuse.
    // Detonation is the only path that sets bExploded or applies blast damage.
    bArmed=false;
    Super::Defeat();
}

void AMCNutKamikaze::Tick(float DeltaSeconds)
{
    // As with the boss subtype, retain AActor ticking and shared hit presentation while replacing the ordinary bite AI.
    AActor::Tick(DeltaSeconds);
    const double Time=ServerNow();
    if(GetNetMode()!=NM_DedicatedServer) {PresentFuse(Time);PresentSummon(Time,DeltaSeconds);}
    if(!HasAuthority() || bDefeated || !bArmed) return;
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    if((Game && (Game->Phase==EMCShiftPhase::Won || Game->Phase==EMCShiftPhase::Lost))
        || !IsValid(Tongue) || Tongue->IsActorBeingDestroyed()) {Defeat();return;}
    // Expiry is independent of contact and target presence. A lost target cannot stop or reset the fuse.
    if(IsDetonationDue(Time)) {Detonate();return;}
    if(UMCCombatPassiveSubsystem::TickBilliards(this,DeltaSeconds)) return;
    if(Time>=NextNearestTargetAt || !IsLiveTarget(Target)) {
        Retarget(); NextNearestTargetAt=Time+.2;
    }
    if(!IsLiveTarget(Target)) return;
    const FVector Direction=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if(!Direction.IsNearlyZero()) {
        SetActorRotation(FMath::RInterpTo(GetActorRotation(),Direction.Rotation(),DeltaSeconds,10.f));
        MoveOnTongue(Direction,DeltaSeconds);
    }
}

void AMCNutKamikaze::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCNutKamikaze,FuseStartedAt); DOREPLIFETIME(AMCNutKamikaze,FuseExplodesAt);
    DOREPLIFETIME(AMCNutKamikaze,bArmed); DOREPLIFETIME(AMCNutKamikaze,bExploded);
    DOREPLIFETIME(AMCNutKamikaze,BlastRadius); DOREPLIFETIME(AMCNutKamikaze,BlastDamage); DOREPLIFETIME(AMCNutKamikaze,PushSpeed);
}
