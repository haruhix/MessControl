#include "MCNutSpellProjectile.h"
#include "MCNutCombatEffect.h"
#include "MCNutCombatVisuals.h"
#include "MCGameState.h"
#include "MCNutRainEvent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"

AMCNutSpellProjectile::AMCNutSpellProjectile()
{
    bReplicates=true;bAlwaysRelevant=true;SetReplicateMovement(false);SetNetUpdateFrequency(20);
    PrimaryActorTick.bCanEverTick=true;
    auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("FlightRoot"));SetRootComponent(Root);
    Nut=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BurningWalnut"));Nut->SetupAttachment(Root);
    Flames=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FireRibbons"));Flames->SetupAttachment(Root);
    Trail=CreateDefaultSubobject<UNiagaraComponent>(TEXT("EmberTrail"));Trail->SetupAttachment(Root);Trail->SetAutoActivate(false);
    MCNutCombatVisuals::Configure(Nut);MCNutCombatVisuals::Configure(Flames);MCNutCombatVisuals::Configure(Trail);
}

double AMCNutSpellProjectile::Now() const
{const auto* GS=GetWorld()->GetGameState();return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();}

FVector AMCNutSpellProjectile::PositionAt(float Age) const
{return FMath::Lerp(Flight.Start,Flight.Target,FMath::Clamp(Age/FMath::Max(.1f,Flight.Seconds),0.f,1.f));}

AMCNutSpellProjectile* AMCNutSpellProjectile::Spawn(AActor* Source,AMCTongue* Surface,FVector Start,FVector Target,float Speed,float Amount,float Radius)
{
    if(!IsValid(Source) || !Source->HasAuthority() || !IsValid(Surface) || Surface->GetWorld()!=Source->GetWorld()
        || Start.ContainsNaN() || Target.ContainsNaN() || !FMath::IsFinite(Speed) || Speed<=0
        || !FMath::IsFinite(Amount) || Amount<=0 || !FMath::IsFinite(Radius) || Radius<=0) return nullptr;
    const FTransform Pose(Start);
    auto* Shot=Source->GetWorld()->SpawnActorDeferred<AMCNutSpellProjectile>(StaticClass(),Pose,Source,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Shot) return nullptr;
    Shot->SourceActor=Source;Shot->Tongue=Surface;Shot->Damage=FMath::Clamp(Amount,1.f,35.f);
    Shot->Flight.Start=Start;Shot->Flight.Target=Target;Shot->Flight.Radius=FMath::Clamp(Radius,8.f,65.f);
    Shot->Flight.Seconds=FMath::Clamp(FVector::Dist(Start,Target)/FMath::Clamp(Speed,100.f,1800.f),.1f,8.f);
    Shot->Flight.StartedAt=Shot->Now();Shot->FinishSpawning(Pose);return Shot;
}

void AMCNutSpellProjectile::BeginPlay()
{
    Super::BeginPlay();if(HasAuthority()) SetLifeSpan(Flight.Seconds+1.f);
    if(Tongue) AddTickPrerequisiteActor(Tongue);
    if(GetNetMode()==NM_DedicatedServer) return;
    Nut->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Gameplay/CoreLoop/SM_Walnut_Whole.SM_Walnut_Whole")));
    if(auto* Parent=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/VFX/M_ReactionFire.M_ReactionFire"))) FireMID=Flames->CreateDynamicMaterialInstance(0,Parent);
    Trail->SetAsset(LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Gameplay/VFX/NutCombat/NS_NutEmberTrail.NS_NutEmberTrail")));
    if(Impact.bImpacted) PresentImpact();
}

void AMCNutSpellProjectile::ResolveImpact(const FHitResult& Hit)
{
    if(!HasAuthority() || Impact.bImpacted) return;
    Impact.bImpacted=true;Impact.Point=Hit.ImpactPoint;Impact.Normal=Hit.ImpactNormal.GetSafeNormal(SMALL_NUMBER,FVector::UpVector);Impact.At=Now();
    if(auto* Hero=Cast<AMCToothCharacter>(Hit.GetActor());Hero && Hero->Status && Hero->Status->IsAlive()) {
        const FVector Direction=(Flight.Target-Flight.Start).GetSafeNormal(SMALL_NUMBER,FVector::ForwardVector);
        if(Hero->Status->Damage(Damage,Direction)) {
            ++DamageApplications;
            if(Hero->ToothPhysics) Hero->ToothPhysics->ApplyHit(Direction*180+FVector(0,0,65),Hit.ImpactPoint);
        }
    }
    if(auto* Effect=AMCNutCombatEffect::Spawn(SourceActor,Tongue,EMCNutCombatCue::FireImpact,Impact.Point,Impact.Point,Flight.Radius*2.6f,0,.65f)) {
        // Keep the transient terminal burst in the encounter cleanup set as well as the projectile.
        if(auto* Event=Cast<AMCNutRainEvent>(GetOwner())) {Effect->SetOwner(Event);Event->TrackEncounterActor(Effect);}
    }
    PresentImpact();ForceNetUpdate();SetLifeSpan(.7f);
}

void AMCNutSpellProjectile::PresentImpact()
{
    if(!Impact.bImpacted) return;
    Nut->SetVisibility(false);Flames->SetVisibility(false);Trail->Deactivate();SetActorLocation(Impact.Point);
    bImpactPresented=true;
}

void AMCNutSpellProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(HasAuthority()) {
        const auto* GS=GetWorld()->GetGameState<AMCGameState>();
        if(!IsValid(SourceActor) || SourceActor->IsActorBeingDestroyed() || !IsValid(Tongue)
            || (GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost))) {Destroy();return;}
    }
    if(Impact.bImpacted) {if(!bImpactPresented) PresentImpact();return;}
    if(Flight.StartedAt<0) return;
    const float Age=FMath::Clamp(float(Now()-Flight.StartedAt),0.f,Flight.Seconds);
    if(HasAuthority()) {
        FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);Objects.AddObjectTypesToQuery(ECC_Pawn);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(MCNutFireball),false,this);Params.AddIgnoredActor(SourceActor);Params.AddIgnoredActor(GetOwner());
        FHitResult Hit;
        // The entire distance since the previous update is swept; low frame rates cannot tunnel through a tooth.
        if(GetWorld()->SweepSingleByObjectType(Hit,PositionAt(PreviousAge),PositionAt(Age),FQuat::Identity,Objects,FCollisionShape::MakeSphere(Flight.Radius),Params)) {ResolveImpact(Hit);return;}
        if(Age>=Flight.Seconds) {Hit.ImpactPoint=Flight.Target;Hit.ImpactNormal=FVector::UpVector;ResolveImpact(Hit);return;}
        PreviousAge=Age;
    }
    SetActorLocation(PositionAt(Age));
    if(GetNetMode()==NM_DedicatedServer) return;
    const float Extent=Nut->GetStaticMesh()?Nut->GetStaticMesh()->GetBounds().BoxExtent.GetMax():1.f;
    Nut->SetRelativeScale3D(FVector(Flight.Radius*.85f/FMath::Max(.1f,Extent)));
    Nut->SetRelativeRotation(FRotator(Age*160,Age*100,Age*65));
    Flames->SetVisibility(FireMID!=nullptr);
    if(FireMID) {FireMID->SetScalarParameterValue(TEXT("FireAge"),Age);MCNutCombatVisuals::RenderFlames(Flames,Flight.Radius,Age,1,(Flight.Start-Flight.Target).GetSafeNormal()*Flight.Radius*2.8f);}
    if(!bTrailStarted && Trail->GetAsset()) {bTrailStarted=true;Trail->Activate(true);}
}

void AMCNutSpellProjectile::Cancel() {if(HasAuthority()) Destroy();}
void AMCNutSpellProjectile::EndPlay(const EEndPlayReason::Type Reason)
{Trail->DeactivateImmediate();Super::EndPlay(Reason);}
void AMCNutSpellProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AMCNutSpellProjectile,Flight);DOREPLIFETIME(AMCNutSpellProjectile,Impact);
    DOREPLIFETIME(AMCNutSpellProjectile,Tongue);DOREPLIFETIME(AMCNutSpellProjectile,SourceActor);
}
