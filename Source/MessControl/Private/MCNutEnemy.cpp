#include "MCNutEnemy.h"
#include "MCDevCommands.h"
#include "MCWorldStatusComponent.h"
#include "MCPerkComponent.h"
#include "MCCombatPassiveSubsystem.h"
#include "MCDefenseSprayPassives.h"

#include "MCFoodActor.h"
#include "MCGameState.h"
#include "MCTongue.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCCombatFeedbackComponent.h"
#include "MCWeaponImpact.h"
#include "MCReactionVFX.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMCNutEnemy::AMCNutEnemy()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); SetNetUpdateFrequency(20);
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.05f;
    Body=CreateDefaultSubobject<USphereComponent>(TEXT("NutBody")); SetRootComponent(Body);
    Body->InitSphereRadius(58); Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Body->SetCollisionObjectType(ECC_WorldDynamic); Body->SetCollisionResponseToAllChannels(ECR_Ignore);
    Body->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);
    Body->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
    Body->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Body->SetGenerateOverlapEvents(false); Body->SetCanEverAffectNavigation(false);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NutMesh")); Visual->SetupAttachment(Body);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetCanEverAffectNavigation(false);
    Feedback=CreateDefaultSubobject<UMCCombatFeedbackComponent>(TEXT("CombatFeedback"));
    LeftEye=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftEye")); LeftEye->SetupAttachment(Body);
    RightEye=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightEye")); RightEye->SetupAttachment(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    for(UStaticMeshComponent* Eye:{LeftEye.Get(),RightEye.Get()})
    {
        Eye->SetStaticMesh(Sphere.Object); Eye->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Eye->SetRelativeScale3D(FVector(.12,.12,.09)); Eye->SetCastShadow(false);
        Eye->SetCanEverAffectNavigation(false);
    }
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Health")); Label->SetupAttachment(Body);
    Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(19);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision); Label->SetTextRenderColor(FColor(255,110,65));
    WorldStatus=CreateDefaultSubobject<UMCWorldStatusComponent>(TEXT("WorldStatus"));
}

void AMCNutEnemy::BeginPlay()
{
    Super::BeginPlay(); Settings.Sanitize(); PhaseOffset=GetUniqueID()*.37f;
    Feedback->SetVisualTarget(Visual);
    if(auto* EyeMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Gameplay/CoreLoop/M_NutEnemyEyes.M_NutEnemyEyes")))
        for(UStaticMeshComponent* Eye:{LeftEye.Get(),RightEye.Get()}) Eye->SetMaterial(0,EyeMaterial);
    RefreshPresentation();
}

void AMCNutEnemy::ConfigureFromFood(const AMCFoodActor* Food,AMCTongue* OnTongue,FMCNutEnemySettings InSettings)
{
    if(!HasAuthority() || !IsValid(Food)) return;
    InSettings.Sanitize();
    FVector Scale=Food->Visual->GetRelativeScale3D()*Food->GetActorScale3D();
    // Awakening uses a player's physical height, rather than retaining the much
    // larger falling-food presentation. Explicit summoned creep scales are separate.
    if(Food->ItemMesh) {
        const double Height=Food->ItemMesh->GetBounds().BoxExtent.Z*2*FMath::Abs(Scale.Z);
        if(FMath::IsFinite(Height) && Height>KINDA_SMALL_NUMBER) Scale*=InSettings.BodyRadius*2.f/Height;
    }
    ConfigureEnemy(OnTongue,Food->ItemMesh,Scale,InSettings);
}

void AMCNutEnemy::ConfigureEnemy(AMCTongue* OnTongue,UStaticMesh* Mesh,FVector Scale,FMCNutEnemySettings InSettings)
{
    if(!HasAuthority() || !IsValid(OnTongue) || !IsValid(Mesh) || Scale.ContainsNaN()) return;
    Settings=InSettings; Settings.Sanitize(); Health=Settings.MaxHealth; bDefeated=false;
    NutMesh=Mesh; MeshScale=Scale;
    Tongue=OnTongue; RefreshPresentation(); ForceNetUpdate();
}

void AMCNutEnemy::RefreshPresentation()
{
    Body->SetSphereRadius(Settings.BodyRadius);
    Body->SetCollisionEnabled(bDefeated?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryOnly);
    if(NutMesh)
    {
        Visual->SetStaticMesh(NutMesh); Visual->SetRelativeScale3D(MeshScale);
        VisualCenter=-NutMesh->GetBounds().Origin*MeshScale;
        Visual->SetRelativeLocation(VisualCenter);
    }
    LeftEye->SetRelativeLocation(FVector(Settings.BodyRadius*.88f,-Settings.BodyRadius*.3f,Settings.BodyRadius*.32f));
    RightEye->SetRelativeLocation(FVector(Settings.BodyRadius*.88f,Settings.BodyRadius*.3f,Settings.BodyRadius*.32f));
    for(UStaticMeshComponent* Eye:{LeftEye.Get(),RightEye.Get()})
        Eye->SetRelativeScale3D(FVector(.12,.12,.09)*(Settings.BodyRadius/42.f));
    LeftEye->SetVisibility(!bDefeated); RightEye->SetVisibility(!bDefeated);
    Label->SetRelativeLocation(FVector(0,0,Settings.BodyRadius+28));
    Label->SetText(FText::FromString(FString::Printf(TEXT("%d"),FMath::CeilToInt(Health))));
    Label->SetVisibility(!bDefeated);
}

FVector AMCNutEnemy::GetToolTargetPoint(FVector From) const
{
    return GetActorLocation()+(From-GetActorLocation()).GetClampedToMaxSize(Settings.BodyRadius);
}

void AMCNutEnemy::ApplyRogueSlow(float Multiplier,float Seconds)
{
    if(!HasAuthority()) return;
    RogueSlowMultiplier=FMath::Clamp(Multiplier,.1f,1.f);RogueSlowUntil=GetWorld()->GetTimeSeconds()+FMath::Max(0.f,Seconds);
}
float AMCNutEnemy::ReceiveToolDamage(float Damage,AMCToothCharacter* Source)
{
    if(!HasAuthority() || !CanReceiveToolHit() || !FMath::IsFinite(Damage) || Damage<=0
        || (Source && (Source->GetWorld()!=GetWorld() || !IsLiveTarget(Source)))) return 0;
    auto* Perks=UMCPerkComponent::ForActor(Source);
    if(Perks) Damage=Perks->WeaponDamage(Damage,this);
    Damage=MCDevEnemyHitDamage(Damage,Health,Source);
    const float Applied=FMath::Min(Health,Damage); Health-=Applied; HitAt=GetWorld()->GetTimeSeconds();
    if(Perks && Applied>0) Perks->NotifyWeaponHit(this);
    ReportDamageFeedback(Source,Applied);
    if(IsValid(Source)) Target=Source;
    // A hit interrupts the telegraphed bite and gives the worker time to move.
    bAttackPending=false; AttackTarget.Reset(); AttackStartedAt=-100;
    NextAttackAt=HitAt+.65f;
    if(Health<=0) Defeat();
    else RefreshPresentation();
    ForceNetUpdate(); return Applied;
}

void AMCNutEnemy::ReportDamageFeedback(AMCToothCharacter* Source,float Applied)
{
    const FVector From=Source?Source->GetActorLocation():GetActorLocation()-GetActorForwardVector()*Settings.BodyRadius*2;
    Feedback->ReportAcceptedHit(Source,GetToolTargetPoint(From),(GetActorLocation()-From).GetSafeNormal(),Applied,0.f,false,Health<=0.f);
}

bool AMCNutEnemy::IsLiveTarget(const AMCToothCharacter* Hero) const
{
    return IsValid(Hero) && !Hero->IsActorBeingDestroyed() && Hero->Status && Hero->Status->IsAlive()
        && !Hero->SwallowedBy && !Hero->IsMimicCaptured();
}

void AMCNutEnemy::Retarget()
{
    Target=nullptr; double Nearest=DBL_MAX;
    for(TActorIterator<AMCToothCharacter> It(GetWorld());It;++It)
        if(IsLiveTarget(*It))
        {
            const double Distance=FVector::DistSquared2D(It->GetActorLocation(),GetActorLocation());
            if(Distance<Nearest) { Nearest=Distance; Target=*It; }
        }
    NextTargetAt=GetWorld()->GetTimeSeconds()+.5;
}

bool AMCNutEnemy::HasAttackContact(const AMCToothCharacter* Hero) const
{
    if(!IsLiveTarget(Hero)) return false;
    const FVector Offset=Hero->GetActorLocation()-GetActorLocation();
    if(Offset.SizeSquared2D()>FMath::Square(Settings.AttackRange) || FMath::Abs(Offset.Z)>180) return false;
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MCNutBite),false,this); Query.AddIgnoredActor(Hero);
    return !GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),Hero->GetActorLocation(),ECC_Visibility,Query);
}

void AMCNutEnemy::Defeat()
{
    if(bDefeated) return;
    bDefeated=true; Health=0; Target=nullptr; bAttackPending=false; AttackTarget.Reset();
    if(Feedback->LastHit.bFatal && !ActorHasTag(TEXT("MC.LegacyCombatFeedback")))
        AMCReactionVFX::Spawn(GetWorld(),GetActorLocation(),EMCReactionEffect::FoodBreak,.45f,
            Settings.BodyRadius*1.6f,Feedback->LastHit.Direction);
    RefreshPresentation(); SetLifeSpan(1.2f); ForceNetUpdate();
}

void AMCNutEnemy::MoveOnTongue(FVector Direction,float Dt)
{
    if(!IsValid(Tongue)) return;
    const FVector Position=GetActorLocation();
    // The mouth has a moving procedural floor. Probe its surface directly rather
    // than asking a static navmesh to follow an out-of-date tongue.
    for(const float Angle:{0.f,45.f,-45.f,90.f,-90.f})
    {
        const float Slow=GetWorld()->GetTimeSeconds()<RogueSlowUntil?RogueSlowMultiplier:1.f;
        const FVector Step=Direction.RotateAngleAxis(Angle,FVector::UpVector)*Settings.MoveSpeed*Slow*FMath::Min(Dt,.1f);
        FHitResult Floor;
        if(!Tongue->InteriorSurfacePoint(Position+Step,Settings.BodyRadius+5,Floor)) continue;
        const FVector Goal=Floor.ImpactPoint+FVector(0,0,Settings.BodyRadius+4);
        FHitResult Obstacle; SetActorLocation(Goal,true,&Obstacle);
        if(!Obstacle.bBlockingHit || FVector::DistSquared2D(Position,GetActorLocation())>1) break;
    }
}

void AMCNutEnemy::TickPresentation(double Now)
{
    if(!bDefeated && MCWeaponImpact::IsPoseHeld(this)) return;
    const bool Winding=AttackStartedAt>-99 && Now-AttackStartedAt<Settings.WindupSeconds;
    // Every procedural monster consumes the same accepted-hit pulse and fixed impact direction.
    const float Hit=!bDefeated && Feedback->bUseProceduralRecoil?Feedback->GetHitPulse():0.f;
    const FVector Away=GetActorTransform().InverseTransformVectorNoScale(Feedback->LastHit.Direction);
    const FVector Recoil=Away.GetSafeNormal2D(SMALL_NUMBER,-FVector::ForwardVector)*(Settings.BodyRadius*.42f*Hit);
    const float Hop=bDefeated?0:Settings.HopHeight*(.5f+.5f*FMath::Sin(Now*9+PhaseOffset))*(1-Hit);
    const float Squash=(Winding?.82f:1.f)*(1-.23f*Hit);
    const float DeathAge=FMath::Max(0.f,float(Now-HitAt));
    const bool PopDeath=bDefeated && Feedback->LastHit.bFatal && !ActorHasTag(TEXT("MC.LegacyCombatFeedback"));
    const float DeathScale=PopDeath?(1+.12f*FMath::Sin(FMath::Clamp(DeathAge/.09f,0.f,1.f)*PI))
        *(1-FMath::SmoothStep(.055f,.16f,DeathAge)):bDefeated?FMath::Clamp(1-DeathAge/.6f,0.f,1.f):1.f;
    Visual->SetRelativeScale3D(MeshScale*FVector(1.f/Squash,1.f/Squash,Squash)*DeathScale);
    Visual->SetRelativeLocation(VisualCenter+Recoil+FVector(0,0,Winding?0:Hop));
    for(UStaticMeshComponent* Eye:{LeftEye.Get(),RightEye.Get()})
    {
        const float Side=Eye==LeftEye.Get()?-1.f:1.f;
        const float HitSquash=1-.23f*Hit;
        FVector EyePoint(Settings.BodyRadius*.88f/HitSquash,Side*Settings.BodyRadius*.3f/HitSquash,Settings.BodyRadius*.32f*HitSquash);
        EyePoint+=Recoil+FVector(0,0,Winding?0:Hop);
        Eye->SetRelativeLocation(EyePoint);
    }
}

void AMCNutEnemy::Tick(float Dt)
{
    Super::Tick(Dt);
    const auto* GS=GetWorld()->GetGameState<AMCGameState>();
    const double Now=GS?GS->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    TickPresentation(Now);
    if(!HasAuthority() || bDefeated) return;
    if(GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost)) return;
    if(UMCCombatPassiveSubsystem::TickBilliards(this,Dt)) return;
    if(!IsValid(Tongue) || Tongue->IsActorBeingDestroyed()) { Defeat(); return; }
    if(Now>=NextTargetAt || (Target && !IsLiveTarget(Target))) Retarget();
    if(bAttackPending && Now>=AttackStartedAt+Settings.WindupSeconds)
    {
        if(HasAttackContact(AttackTarget.Get()))
            AttackTarget->Status->Damage(Settings.AttackDamage,(AttackTarget->GetActorLocation()-GetActorLocation()).GetSafeNormal2D(),this);
        else if(IsLiveTarget(AttackTarget.Get())) {
            UMCDefenseSprayPassives::NotifyDodgedAttack(AttackTarget.Get(),this,uint64(AttackStartedAt*1000000));
            if(auto* Perks=UMCPerkComponent::ForActor(AttackTarget.Get())) Perks->NotifyDodgedAttack();
        }
        bAttackPending=false; AttackTarget.Reset(); NextAttackAt=Now+Settings.AttackCooldown;
    }
    if(!IsLiveTarget(Target)) return;
    const FVector Direction=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if(!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation());
    if(!bAttackPending && Now>=NextAttackAt && HasAttackContact(Target))
    {
        bAttackPending=true; AttackTarget=Target; AttackStartedAt=Now; ForceNetUpdate();
        UMCDefenseSprayPassives::RegisterThreat(AttackTarget.Get(),this,uint64(AttackStartedAt*1000000));
    }
    if(!bAttackPending)
    {
        if(FVector::DistSquared2D(Target->GetActorLocation(),GetActorLocation())>FMath::Square(Settings.AttackRange*.8f)) MoveOnTongue(Direction,Dt);
        else
        {
            FHitResult Floor;
            if(Tongue->SurfacePoint(GetActorLocation(),Floor))
                SetActorLocation(Floor.ImpactPoint+FVector(0,0,Settings.BodyRadius+4),false);
        }
    }
}

void AMCNutEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMCNutEnemy,NutMesh); DOREPLIFETIME(AMCNutEnemy,MeshScale); DOREPLIFETIME(AMCNutEnemy,Settings);
    DOREPLIFETIME(AMCNutEnemy,Health); DOREPLIFETIME(AMCNutEnemy,bDefeated); DOREPLIFETIME(AMCNutEnemy,Target);
    DOREPLIFETIME(AMCNutEnemy,AttackStartedAt); DOREPLIFETIME(AMCNutEnemy,HitAt); DOREPLIFETIME(AMCNutEnemy,Tongue);
}
