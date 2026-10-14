#include "MCCataclysmJelly.h"

#include "MCCombatFeedbackComponent.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMCCataclysmJelly::AMCCataclysmJelly()
{
    PrimaryActorTick.TickInterval=.025f;
    Settings.MaxHealth=360.f;
    Settings.MoveSpeed=190.f;
    Settings.BodyRadius=72.f;
    Settings.AttackRange=140.f;
    Settings.AttackDamage=0.f;
    Settings.HopHeight=38.f;
    Health=Settings.MaxHealth;
    MeshScale=FVector(1.68f,1.58f,1.30f);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    NutMesh=Sphere.Object;
    Visual->SetBoundsScale(1.2f);
    GroundBurst=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("JellyGroundBurst"));
    GroundBurst->SetupAttachment(Body);
    GroundBurst->SetStaticMesh(Sphere.Object);
    GroundBurst->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GroundBurst->SetCanEverAffectNavigation(false);
    GroundBurst->SetCastShadow(false);
    GroundBurst->SetVisibility(false);
    for(int32 Index=0;Index<10;++Index)
    {
        const FName Name(*FString::Printf(TEXT("JellyDroplet_%02d"),Index));
        auto* Droplet=CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Droplet->SetupAttachment(Body);
        Droplet->SetStaticMesh(Sphere.Object);
        Droplet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Droplet->SetCanEverAffectNavigation(false);
        Droplet->SetCastShadow(false);
        Droplet->SetVisibility(false);
        Droplets.Add(Droplet);
    }
}

double AMCCataclysmJelly::ServerTime() const
{
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    return GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
}

void AMCCataclysmJelly::BeginPlay()
{
    if(GetNetMode()!=NM_DedicatedServer)
        GelMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Cataclysm/Jelly/M_CataclysmJellyGel.M_CataclysmJellyGel"));
    Super::BeginPlay();
    if(HasAuthority() && HopStartedAt<-99.) HopStartedAt=ServerTime();
    LastTrailLocation=GetActorLocation();
    NextTrailAt=ServerTime()+HopSeconds;
    RefreshPresentation();
}

void AMCCataclysmJelly::ConfigureJelly(AMCTongue* OnTongue,float InMaxHealth)
{
    if(!HasAuthority() || !IsValid(OnTongue)) return;
    FMCNutEnemySettings JellySettings=Settings;
    JellySettings.MaxHealth=FMath::IsFinite(InMaxHealth)?FMath::Clamp(InMaxHealth,40.f,1000.f):360.f;
    JellySettings.AttackDamage=0.f;
    bPendingRupture=false;
    DeathStartedAt=RupturedAt=-100.;
    HopStartedAt=ServerTime();
    ConfigureEnemy(OnTongue,NutMesh,MeshScale,JellySettings);
    LastTrailLocation=GetActorLocation();
    NextTrailAt=HopStartedAt+HopSeconds;
    NextContactAt=NextRetargetAt=HopStartedAt;
    ForceNetUpdate();
}

bool AMCCataclysmJelly::IsEncounterAlive() const
{
    return !bDefeated && !IsActorBeingDestroyed() && (Health>0.f || bPendingRupture);
}

bool AMCCataclysmJelly::CanReceiveToolHit() const
{
    return !bPendingRupture && !bDefeated && Health>0.f && !IsActorBeingDestroyed();
}

void AMCCataclysmJelly::RefreshPresentation()
{
    Super::RefreshPresentation();
    Body->SetCollisionEnabled(CanReceiveToolHit()?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    if(GetNetMode()==NM_DedicatedServer) return;
    if(GelMaterial && !GelMID) GelMID=Visual->CreateDynamicMaterialInstance(0,GelMaterial);
    if(GelMaterial && !SplatterMID)
    {
        SplatterMID=UMaterialInstanceDynamic::Create(GelMaterial,this);
        SplatterMID->SetScalarParameterValue(TEXT("JellyWobble"),.15f);
        SplatterMID->SetVectorParameterValue(TEXT("JellyColor"),FLinearColor(.15f,.14f,.03f));
        SplatterMID->SetVectorParameterValue(TEXT("JellyRimColor"),FLinearColor(.37f,.41f,.07f));
    }
    if(GelMID)
    {
        Visual->SetMaterial(0,GelMID);
    }
    if(SplatterMID)
    {
        GroundBurst->SetMaterial(0,SplatterMID);
        for(UStaticMeshComponent* Droplet:Droplets) if(Droplet) Droplet->SetMaterial(0,SplatterMID);
    }
    Label->SetText(bPendingRupture?FText::FromString(TEXT("!")):FText::AsNumber(FMath::CeilToInt(Health)));
    Label->SetTextRenderColor(bPendingRupture?FColor(255,181,48):FColor(203,224,93));
    Label->SetVisibility(!bDefeated);
    LeftEye->SetVisibility(!bDefeated);
    RightEye->SetVisibility(!bDefeated);
}

void AMCCataclysmJelly::TickGelPresentation(double Now)
{
    if(GetNetMode()==NM_DedicatedServer) return;
    const float Phase=FMath::Fmod(FMath::Max(0.,Now-HopStartedAt),double(HopSeconds))/HopSeconds;
    const float Air=FMath::Clamp((Phase-.18f)/.60f,0.f,1.f);
    const float Hop=(Phase>.18f && Phase<.78f)?FMath::Sin(Air*PI):0.f;
    const float Low=FMath::Clamp(1.f-Health/FMath::Max(1.f,Settings.MaxHealth)*3.f,0.f,1.f);
    const float Warning=bPendingRupture?FMath::Clamp(float(Now-DeathStartedAt)/RuptureWarningSeconds,0.f,1.f):0.f;
    const float Hit=Feedback && CanReceiveToolHit()?Feedback->GetHitPulse():0.f;
    const float Shiver=FMath::Sin(float(Now)*53.f)*(Low*.035f+Warning*.08f);
    float Squash=1.f;
    if(Phase<.18f) Squash=1.f-.24f*FMath::Sin(Phase/.18f*PI);
    else if(Phase<.78f) Squash=1.f+.20f*Hop;
    else Squash=1.f-.18f*FMath::Sin((Phase-.78f)/.22f*PI);
    if(bPendingRupture) Squash=.88f+Warning*.20f;
    Squash=FMath::Max(.6f,Squash-.18f*Hit);
    const float Puff=1.f+Low*.13f+Warning*.42f;
    const float BurstAge=RupturedAt>-99.?float(Now-RupturedAt):-1.f;
    const float Collapse=bDefeated?FMath::Clamp(1.f-BurstAge/.18f,0.f,1.f):1.f;
    const FVector Shape=MeshScale*FVector(Puff/FMath::Sqrt(Squash)+Shiver,Puff/FMath::Sqrt(Squash)-Shiver,Puff*Squash)*Collapse;
    Visual->SetRelativeScale3D(Shape);
    // Keep the bottom of the gel seated through squash/stretch; only the air phase lifts it.
    const float Lift=bPendingRupture || bDefeated?0.f:Hop*Settings.HopHeight;
    const FVector Recoil=Feedback?GetActorTransform().InverseTransformVectorNoScale(Feedback->LastHit.Direction).GetSafeNormal2D()*Hit*12.f:FVector::ZeroVector;
    Visual->SetRelativeLocation(FVector(0,0,-Settings.BodyRadius-4.f+Shape.Z*50.f+Lift)+Recoil);
    Visual->SetVisibility(Collapse>.005f);
    Label->SetRelativeLocation(FVector(0,0,-Settings.BodyRadius-4.f+Shape.Z*100.f+Lift+28.f));
    for(UStaticMeshComponent* Eye:{LeftEye.Get(),RightEye.Get()})
    {
        const float Side=Eye==LeftEye.Get()?-1.f:1.f;
        Eye->SetRelativeLocation(FVector(Shape.X*43.f,Side*Shape.Y*17.f,-Settings.BodyRadius-4.f+Shape.Z*62.f+Lift)+Recoil);
        Eye->SetRelativeScale3D(FVector(.14f,.10f,bPendingRupture?.05f:.09f));
    }
    if(GelMID)
    {
        GelMID->SetScalarParameterValue(TEXT("JellyWarning"),FMath::Max(Low*.6f,Warning));
        GelMID->SetScalarParameterValue(TEXT("JellyWobble"),2.f+Low*3.f+Warning*5.f);
        GelMID->SetScalarParameterValue(TEXT("JellyHit"),Hit);
    }
    const bool BurstVisible=BurstAge>=0.f && BurstAge<.48f;
    GroundBurst->SetVisibility(BurstVisible);
    if(BurstVisible)
    {
        const float Spread=FMath::Clamp(BurstAge/.24f,0.f,1.f);
        const float Fade=FMath::Clamp((.48f-BurstAge)/.18f,0.f,1.f);
        GroundBurst->SetRelativeLocation(FVector(0,0,-Settings.BodyRadius-2.f));
        GroundBurst->SetRelativeScale3D(FVector(RuptureRadius*.02f*Spread,RuptureRadius*.02f*Spread,.028f*Fade));
    }
    for(int32 Index=0;Index<Droplets.Num();++Index)
    {
        UStaticMeshComponent* Droplet=Droplets[Index];
        if(!Droplet) continue;
        const bool Visible=BurstAge>=0.f && BurstAge<.70f;
        Droplet->SetVisibility(Visible);
        if(!Visible) continue;
        const float T=FMath::Clamp(BurstAge/.70f,0.f,1.f);
        const float Angle=Index*2.f*PI/Droplets.Num()+.13f*(Index%3);
        const float Distance=(.62f+.08f*(Index%4))*RuptureRadius*T;
        const float Z=Settings.BodyRadius*.5f*(1.f-T)+FMath::Sin(T*PI)*(50.f+Index%3*20.f);
        Droplet->SetRelativeLocation(FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,Z-Settings.BodyRadius));
        Droplet->SetRelativeScale3D(FVector(.20f,.20f,.30f)*(1.f-T*.9f));
    }
}

void AMCCataclysmJelly::ContaminateNearby(float Radius,float Amount)
{
    if(!HasAuthority()) return;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
    {
        AMCToothCharacter* Hero=*It;
        if(!IsPlayerTarget(Hero)) continue;
        const FVector Delta=Hero->GetActorLocation()-GetActorLocation();
        if(Delta.SizeSquared2D()>FMath::Square(Radius) || FMath::Abs(Delta.Z)>160.f) continue;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MCJellyGrime),false,this);
        Query.AddIgnoredActor(Hero);
        if(GetOwner()) Query.AddIgnoredActor(GetOwner());
        if(GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Hero->GetActorLocation(),ECC_Visibility,Query)) continue;
        const float Before=Hero->Status->CoffeeAmount();
        const float After=FMath::Clamp(Before+Amount,0.f,1.f);
        if(After>Before+KINDA_SMALL_NUMBER) Hero->Status->ApplyCoffee(After);
    }
}

void AMCCataclysmJelly::RequestDirtyPatch(float Radius,bool bDeathBurst)
{
    if(!HasAuthority() || !IsValid(Tongue)) return;
    FHitResult Floor;
    if(Tongue->InteriorSurfacePoint(GetActorLocation(),Settings.BodyRadius+5.f,Floor))
        OnDirtyPatchRequested.Broadcast(Floor.ImpactPoint+Floor.ImpactNormal*2.f,Radius,bDeathBurst);
}

bool AMCCataclysmJelly::IsPlayerTarget(const AMCToothCharacter* Hero) const
{
    const auto* Identity=Hero?Hero->GetPlayerState():nullptr;
    return IsLiveTarget(Hero) && Identity && !Identity->IsOnlyASpectator() && !Identity->IsSpectator();
}

void AMCCataclysmJelly::RetargetJelly()
{
    Target=nullptr;double Best=DBL_MAX;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It) if(IsPlayerTarget(*It)) {
        const double Distance=FVector::DistSquared2D(It->GetActorLocation(),GetActorLocation());
        if(Distance<Best) {Best=Distance;Target=*It;}
    }
}

void AMCCataclysmJelly::Defeat()
{
    if(!HasAuthority() || bPendingRupture || bDefeated) return;
    Health=0.f;
    Target=nullptr;
    AttackStartedAt=-100.;
    bPendingRupture=true;
    DeathStartedAt=ServerTime();
    RefreshPresentation();
    ForceNetUpdate();
}

void AMCCataclysmJelly::Rupture(double Now)
{
    if(!HasAuthority() || bDefeated) return;
    // Commit gameplay once, before the short cosmetic lifetime.
    bPendingRupture=false;
    bDefeated=true;
    RupturedAt=Now;
    ContaminateNearby(RuptureRadius,1.f);
    RequestDirtyPatch(RuptureRadius,true);
    RefreshPresentation();
    ForceNetUpdate();
    SetLifeSpan(1.0f);
}

void AMCCataclysmJelly::Tick(float DeltaSeconds)
{
    // Do not call AMCNutEnemy::Tick: its bite scheduler is intentionally unused.
    AActor::Tick(DeltaSeconds);
    const double Now=ServerTime();
    TickGelPresentation(Now);
    if(!HasAuthority() || bDefeated) return;
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    if(GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost)) return;
    if(bPendingRupture)
    {
        if(Now>=DeathStartedAt+RuptureWarningSeconds) Rupture(Now);
        return;
    }
    if(!IsValid(Tongue) || Tongue->IsActorBeingDestroyed()) { Destroy(); return; }
    if(Now>=NextRetargetAt || !IsPlayerTarget(Target)) { RetargetJelly(); NextRetargetAt=Now+.5; }
    const float Phase=FMath::Fmod(FMath::Max(0.,Now-HopStartedAt),double(HopSeconds))/HopSeconds;
    if(IsPlayerTarget(Target))
    {
        const FVector Direction=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
        if(!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation());
        if(Phase>.18f && Phase<.78f && FVector::DistSquared2D(Target->GetActorLocation(),GetActorLocation())>FMath::Square(Settings.BodyRadius+28.f))
            MoveOnTongue(Direction,DeltaSeconds);
        else
        {
            FHitResult Floor;
            if(Tongue->SurfacePoint(GetActorLocation(),Floor)) SetActorLocation(Floor.ImpactPoint+FVector(0,0,Settings.BodyRadius+4.f),false);
        }
    }
    if(Now>=NextContactAt) { ContaminateNearby(Settings.BodyRadius+48.f,.25f); NextContactAt=Now+.8; }
    if(Phase<.16f && Now>=NextTrailAt && FVector::DistSquared2D(LastTrailLocation,GetActorLocation())>FMath::Square(45.f))
    {
        RequestDirtyPatch(TrailRadius,false);
        LastTrailLocation=GetActorLocation();
        NextTrailAt=Now+HopSeconds*.75f;
    }
}

void AMCCataclysmJelly::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    OnDirtyPatchRequested.Clear();
    Super::EndPlay(EndPlayReason);
}

void AMCCataclysmJelly::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCCataclysmJelly,HopStartedAt);
    DOREPLIFETIME(AMCCataclysmJelly,DeathStartedAt);
    DOREPLIFETIME(AMCCataclysmJelly,RupturedAt);
    DOREPLIFETIME(AMCCataclysmJelly,bPendingRupture);
}
