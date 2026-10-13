#include "MCNutKamikaze.h"

#include "MCGameState.h"
#include "MCNutCombatEffect.h"
#include "MCNutCombatVisuals.h"
#include "MCNutRainEvent.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothPhysicsComponent.h"
#include "MCToothStatusComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace
{
float SafeValue(float Value,float Default,float Minimum,float Maximum)
{return FMath::Clamp(FMath::IsFinite(Value)?Value:Default,Minimum,Maximum);}
}

AMCNutKamikaze::AMCNutKamikaze()
{
    Settings.MaxHealth=40;
    Health=Settings.MaxHealth;
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
        if(auto* Material=FuseMaterial.LoadSynchronous()) FuseMID=FuseFlame->CreateDynamicMaterialInstance(0,Material);
        if(auto* Material=WarningMaterial.LoadSynchronous()) WarningMID=BlastWarning->CreateDynamicMaterialInstance(0,Material);
        PresentFuse(ServerNow());
    }
}

bool AMCNutKamikaze::ConfigureKamikaze(AMCTongue* OnTongue,UStaticMesh* Mesh,FVector Scale,FMCNutEnemySettings InSettings,
    float FuseSeconds,float InBlastRadius,float InBlastDamage,float InPushSpeed)
{
    if(!HasAuthority() || !IsValid(OnTongue) || OnTongue->GetWorld()!=GetWorld() || !IsValid(Mesh) || Scale.ContainsNaN()) return false;
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

float AMCNutKamikaze::GetFuseSecondsRemaining() const
{return bArmed && !bDefeated?FMath::Max(0.f,float(FuseExplodesAt-ServerNow())):0.f;}

void AMCNutKamikaze::RefreshPresentation()
{
    Super::RefreshPresentation();
    if(FuseFlame && BlastWarning) {
        FuseFlame->SetVisibility(bArmed && !bDefeated && FuseMID);
        BlastWarning->SetVisibility(false);
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
    // A growing pulse and accelerating fuse are readable before the final blast footprint appears.
    const float Pulse=.5f+.5f*FMath::Sin(Age*(5.f+Progress*15.f));
    const float Swell=1.f+Progress*.12f+Pulse*Progress*.08f;
    Visual->SetRelativeScale3D(Visual->GetRelativeScale3D()*Swell);
    Label->SetText(FText::FromString(FString::Printf(TEXT("%d HP | %.1f s"),FMath::CeilToInt(Health),Remaining)));
    Label->SetTextRenderColor(FColor(255,FMath::RoundToInt(FMath::Lerp(190.f,45.f,Progress)),30));
    if(FuseMID) {
        FuseFlame->SetRelativeLocation(Visual->GetRelativeLocation()+FVector(0,0,Settings.BodyRadius*.8f));
        FuseMID->SetScalarParameterValue(TEXT("FireAge"),Age);
        MCNutCombatVisuals::RenderFlames(FuseFlame,7.f+Progress*5.f,Age,.8f);
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
    if(GetNetMode()!=NM_DedicatedServer) PresentFuse(Time);
    if(!HasAuthority() || bDefeated || !bArmed) return;
    const auto* Game=GetWorld()->GetGameState<AMCGameState>();
    if((Game && (Game->Phase==EMCShiftPhase::Won || Game->Phase==EMCShiftPhase::Lost))
        || !IsValid(Tongue) || Tongue->IsActorBeingDestroyed()) {Defeat();return;}
    // Expiry is independent of contact and target presence. A lost target cannot stop or reset the fuse.
    if(Time>=FuseExplodesAt) {Detonate();return;}
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
