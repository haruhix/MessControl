#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

void FMCFoodSettings::Sanitize()
{
    auto Safe=[](float V,float D,float L,float H){return FMath::IsFinite(V)?FMath::Clamp(V,L,H):D;};
    DropHeight=Safe(DropHeight,650,200,2000); Mass=Safe(Mass,9,1,50);
    ImpactSpeed=Safe(ImpactSpeed,180,50,1500); DamagePerSpeed=Safe(DamagePerSpeed,.035f,0,1);
    MaxDamage=Safe(MaxDamage,40,0,1000); Knockback=Safe(Knockback,560,150,1500); HitCooldown=Safe(HitCooldown,.75f,.2f,10);
    GrabReach=Safe(GrabReach,145,50,250); BreakDistance=Safe(BreakDistance,340,GrabReach+50,600);
    Spring=Safe(Spring,14,1,50); Damping=Safe(Damping,6,1,30); PullSeconds=Safe(PullSeconds,3,.2f,30);
    PullConeDegrees=Safe(PullConeDegrees,35,10,90); CooperationMultiplier=Safe(CooperationMultiplier,1.5f,1,4);
}
AMCFoodActor::AMCFoodActor()
{
    bReplicates=true; bAlwaysRelevant=true; SetReplicateMovement(true); SetNetUpdateFrequency(30);
    PrimaryActorTick.bCanEverTick=true;
    Body=CreateDefaultSubobject<UBoxComponent>(TEXT("FoodBody")); SetRootComponent(Body);
    Body->SetBoxExtent(FVector(48,32,30)); Body->SetCollisionProfileName(TEXT("PhysicsActor"));
    Body->SetNotifyRigidBodyCollision(true); Body->BodyInstance.bUseCCD=true;
    Body->SetLinearDamping(.7f); Body->SetAngularDamping(2.f);
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FoodMesh")); Visual->SetupAttachment(Body);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetRelativeLocation(FVector(0,0,-25)); Visual->SetRelativeScale3D(FVector(1.5));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Art/Meshes/SM_Food"));
    if (Mesh.Succeeded()) Visual->SetStaticMesh(Mesh.Object);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("FoodInstruction")); Label->SetupAttachment(Body);
    Label->SetRelativeLocation(FVector(0,0,75)); Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(17);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision); Label->SetTextRenderColor(FColor(255,215,110));
    Profile=TSoftObjectPtr<UMCFoodProfile>(FSoftObjectPath(TEXT("/Game/Data/DA_FoodPhysics.DA_FoodPhysics")));
}
void AMCFoodActor::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority()) { if (const auto* P=Profile.LoadSynchronous()) Settings=P->Settings; Settings.Sanitize(); }
    Body->SetMassOverrideInKg(NAME_None,Settings.Mass,true);
    Body->OnComponentHit.AddDynamic(this,&AMCFoodActor::OnHit); OnRep_Phase();
}
void AMCFoodActor::Initialize(bool bJam,FVector ExtractionDirection)
{
    if (!HasAuthority()) return;
    bJamOnLanding=bJam; PullDirection=ExtractionDirection.GetSafeNormal2D();
    if (PullDirection.IsNearlyZero()) PullDirection=FVector(0,1,0);
}
void AMCFoodActor::OnRep_Phase()
{
    const bool bGone=IsDisposed();
    const bool bSimulate=HasAuthority() && (Phase==EMCFoodPhase::Falling || Phase==EMCFoodPhase::Free);
    // Stop Chaos before disabling collision, including disposal while the item is still moving.
    if (!bSimulate) Body->SetSimulatePhysics(false);
    SetActorHiddenInGame(bGone); Body->SetCollisionEnabled(bGone?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
    // Simulated proxies are kinematic; only the server applies springs and impact damage.
    if (bSimulate) Body->SetSimulatePhysics(true);
}
bool AMCFoodActor::TryGrab(AMCToothCharacter* Hero)
{
    if (!HasAuthority() || !IsValid(Hero) || IsDisposed() || !Hero->CanWork() || Hero->HeldFood && Hero->HeldFood!=this) return false;
    if (Holders.Contains(Hero)) return true;
    const FVector Offset=GetActorLocation()-Hero->GetActorLocation();
    if (Offset.Size()>Settings.GrabReach || FVector::DotProduct(Hero->GetActorForwardVector(),Offset.GetSafeNormal2D())<-.25f) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFoodGrab),false,Hero); Params.AddIgnoredActor(this);
    if (GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation(),GetActorLocation(),ECC_Visibility,Params)) return false;
    Holders.Add(Hero); GripOffsets.Add(Hero,Offset.GetClampedToMaxSize(95)); Hero->HeldFood=this;
    Hero->ForceNetUpdate(); ForceNetUpdate(); return true;
}
void AMCFoodActor::Release(AMCToothCharacter* Hero)
{
    if (!HasAuthority()) return;
    Holders.Remove(Hero); GripOffsets.Remove(Hero);
    if (IsValid(Hero) && Hero->HeldFood==this) { Hero->HeldFood=nullptr; Hero->ForceNetUpdate(); }
    ForceNetUpdate();
}
void AMCFoodActor::Dispose()
{
    if (!HasAuthority() || IsDisposed()) return;
    for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]);
    Phase=EMCFoodPhase::Disposed; OnRep_Phase(); ForceNetUpdate(); SetLifeSpan(3);
}
void AMCFoodActor::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority()) for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]);
    Super::EndPlay(Reason);
}
void AMCFoodActor::OnHit(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent* OtherComponent,FVector Impulse,const FHitResult& Hit)
{
    if (!HasAuthority() || IsDisposed() || !IsValid(Other)) return;
    if (Phase==EMCFoodPhase::Falling && Hit.ImpactNormal.Z>.6f && OtherComponent && OtherComponent->GetCollisionObjectType()==ECC_WorldStatic)
        bLandingPending=true;
    UMCToothStatusComponent* Target=Other->FindComponentByClass<UMCToothStatusComponent>();
    if (!Target || !Target->IsAlive() || Phase==EMCFoodPhase::Stuck) return;
    if (const auto* Arena=Cast<AMCArenaTooth>(Other); Arena && !Arena->IsAvailable()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if (const double* Prev=LastHit.Find(Other); Prev && Now-*Prev<Settings.HitCooldown) return;
    const float Speed=FMath::Max((PreviousVelocity-Other->GetVelocity()).Size(),Impulse.Size()/Settings.Mass);
    if (Speed<Settings.ImpactSpeed) return;
    LastHit.Add(Other,Now); ++ConfirmedImpacts;
    FVector Direction=(Other->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if (Direction.IsNearlyZero()) Direction=FVector::ForwardVector;
    Target->Damage(FMath::Min(Settings.MaxDamage,Speed*Settings.DamagePerSpeed*Settings.Mass/9.f),Direction);
    if (auto* Hero=Cast<AMCToothCharacter>(Other))
    {
        Hero->DropFood();
        Hero->ToothPhysics->ApplyHit(Direction*Settings.Knockback+FVector(0,0,Settings.Knockback*.5f),Hit.ImpactPoint);
    }
}
void AMCFoodActor::Tick(float Dt)
{
    Super::Tick(Dt);
    if (HasAuthority() && !IsDisposed())
    {
        if (bLandingPending && Phase==EMCFoodPhase::Falling)
        { bLandingPending=false; Phase=bJamOnLanding?EMCFoodPhase::Stuck:EMCFoodPhase::Free; OnRep_Phase(); ForceNetUpdate(); }
        if (const auto* GS=GetWorld()->GetGameState<AMCGameState>(); GS && (GS->Phase==EMCShiftPhase::Won || GS->Phase==EMCShiftPhase::Lost))
            for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]);
        FVector Target=FVector::ZeroVector; int32 Pullers=0;
        for (int32 I=Holders.Num()-1;I>=0;--I)
        {
            AMCToothCharacter* Hero=Holders[I];
            if (!IsValid(Hero) || !Hero->CanWork() || !Hero->bHandling || FVector::Dist(Hero->GetActorLocation(),GetActorLocation())>Settings.BreakDistance)
            {
#if !UE_BUILD_SHIPPING
                if (FParse::Param(FCommandLine::Get(),TEXT("MCCore"))) UE_LOG(LogTemp,Display,TEXT("MC_CORE_RELEASE hero=%s distance=%.1f canwork=%d handling=%d"),*GetNameSafe(Hero),IsValid(Hero)?FVector::Dist(Hero->GetActorLocation(),GetActorLocation()):-1,IsValid(Hero)&&Hero->CanWork(),IsValid(Hero)&&Hero->bHandling);
#endif
                Release(Hero); continue;
            }
            Target+=Hero->GetActorLocation()+GripOffsets.FindRef(Hero);
            const FVector Velocity=Hero->GetVelocity();
            if (Velocity.Size2D()>10 && FVector::DotProduct(Velocity.GetSafeNormal2D(),PullDirection)>=FMath::Cos(FMath::DegreesToRadians(Settings.PullConeDegrees))) ++Pullers;
        }
        if (Phase==EMCFoodPhase::Stuck)
        {
            const float Now=GetWorld()->GetTimeSeconds();
            if (Pullers>0) { LastPullTime=Now; PullProgress=FMath::Min(1.f,PullProgress+Dt*(Pullers>1?Settings.CooperationMultiplier:1.f)/Settings.PullSeconds); }
            else if (Now-LastPullTime>1) PullProgress=FMath::Max(0.f,PullProgress-Dt*.2f);
            if (PullProgress>=1) { Phase=EMCFoodPhase::Free; OnRep_Phase(); Body->AddImpulse(PullDirection*120+FVector(0,0,80),NAME_None,true); ForceNetUpdate(); }
        }
        else if (!Holders.IsEmpty())
        {
            Target/=Holders.Num();
            const FVector Acceleration=((Target-GetActorLocation())*Settings.Spring-Body->GetPhysicsLinearVelocity()*Settings.Damping).GetClampedToMaxSize(2000);
            Body->AddForce(Acceleration*Settings.Mass);
        }
        // An escaped item returns to the arena, never counts as successfully disposed.
        if (GetActorLocation().Z<-250)
        { for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]); SetActorLocation(FVector(0,0,Settings.DropHeight),false,nullptr,ETeleportType::TeleportPhysics); Body->SetPhysicsLinearVelocity(FVector::ZeroVector); }
        PreviousVelocity=Body->GetPhysicsLinearVelocity();
    }
    Label->SetText(FText::FromString(Phase==EMCFoodPhase::Stuck?FString::Printf(TEXT("E + MOVE TO CENTRE\nPULL %.0f%% | %d GRIPS"),PullProgress*100,Holders.Num()):TEXT("HOLD E: DRAG\nTO THROAT >>>")));
    if (const auto* PC=GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}
void AMCFoodActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCFoodActor,Settings); DOREPLIFETIME(AMCFoodActor,Phase);
    DOREPLIFETIME(AMCFoodActor,PullProgress); DOREPLIFETIME(AMCFoodActor,PullDirection); DOREPLIFETIME(AMCFoodActor,Holders);
}
AMCFoodDisposal::AMCFoodDisposal()
{
    PrimaryActorTick.bCanEverTick=true; bReplicates=true; bAlwaysRelevant=true;
    Volume=CreateDefaultSubobject<UBoxComponent>(TEXT("ThroatVolume")); SetRootComponent(Volume);
    Volume->SetBoxExtent(FVector(105,290,180)); Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ThroatLabel")); Label->SetupAttachment(Volume);
    Label->SetRelativeRotation(FRotator(0,180,0)); Label->SetHorizontalAlignment(EHTA_Center); Label->SetWorldSize(30);
    Label->SetText(FText::FromString(TEXT("FOOD >>> THROAT\nBRING FOOD HERE"))); Label->SetTextRenderColor(FColor(115,255,210));
}
void AMCFoodDisposal::Tick(float Dt)
{
    Super::Tick(Dt); if (!HasAuthority()) return;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if (It->Phase==EMCFoodPhase::Free && Volume->Bounds.GetBox().IsInside(It->GetActorLocation())) It->Dispose();
}
