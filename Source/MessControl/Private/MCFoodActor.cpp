#include "MCFoodActor.h"
#include "MCToothCharacter.h"
#include "MCToothStatusComponent.h"
#include "MCToothPhysicsComponent.h"
#include "MCArenaTooth.h"
#include "MCGameState.h"
#include "MCMouthSurface.h"
#include "Kismet/GameplayStatics.h"
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
    if (!ItemName.IsNone()) Settings.Mass=FoodData.Mass;
    if (bBrushTool) Settings.Mass=1;
    Body->SetMassOverrideInKg(NAME_None,Settings.Mass,true);
    Body->OnComponentHit.AddDynamic(this,&AMCFoodActor::OnHit); OnRep_Phase(); OnRep_Item();
    if (HasAuthority() && !ItemName.IsNone() && SpoilAt<=0) SpoilAt=GetWorld()->GetTimeSeconds()+FoodData.SpoilSeconds;
}
void AMCFoodActor::Initialize(bool bJam,FVector ExtractionDirection)
{
    if (!HasAuthority()) return;
    bJamOnLanding=bJam; PullDirection=ExtractionDirection.GetSafeNormal2D();
    if (PullDirection.IsNearlyZero()) PullDirection=FVector(0,1,0);
}
void AMCFoodActor::OnRep_ReplicatedMovement()
{
    if (HasAuthority()) return;
    // Our proxies deliberately do not simulate. AActor's default physics path sends
    // a rigid-body target, which stops moving them after OnRep_Phase disables Chaos.
    // Consume the same replicated transform as a kinematic presentation instead.
    Body->SetSimulatePhysics(false);
    const auto& Motion=GetReplicatedMovement();
    NetworkLocation=FRepMovement::RebaseOntoLocalOrigin(Motion.Location,this);
    NetworkRotation=Motion.Rotation.Quaternion();
    if (!bReceivedMotion || FVector::DistSquared(GetActorLocation(),NetworkLocation)>FMath::Square(600.f) || Phase==EMCFoodPhase::Stuck)
        SetActorLocationAndRotation(NetworkLocation,NetworkRotation,false,nullptr,ETeleportType::TeleportPhysics);
    bReceivedMotion=true;
}
void AMCFoodActor::OnRep_Phase()
{
    const bool bGone=IsDisposed() || Phase==EMCFoodPhase::Equipped;
    const bool bSimulate=HasAuthority() && (Phase==EMCFoodPhase::Falling || Phase==EMCFoodPhase::Free);
    // Stop Chaos before disabling collision, including disposal while the item is still moving.
    if (!bSimulate) Body->SetSimulatePhysics(false);
    SetActorHiddenInGame(bGone); Body->SetCollisionEnabled(bGone?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
    // Simulated proxies are kinematic; only the server applies springs and impact damage.
    if (bSimulate) Body->SetSimulatePhysics(true);
}
bool AMCFoodActor::TryGrab(AMCToothCharacter* Hero)
{
    if (!HasAuthority() || !IsValid(Hero) || IsDisposed() || Phase==EMCFoodPhase::Equipped || !Hero->CanWork() || Hero->HeldFood && Hero->HeldFood!=this) return false;
    if (Holders.Contains(Hero)) return true;
    const FVector Offset=GetActorLocation()-Hero->GetActorLocation();
    if (Offset.Size()>Settings.GrabReach || FVector::DotProduct(Hero->GetActorForwardVector(),Offset.GetSafeNormal2D())<-.25f) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFoodGrab),false,Hero); Params.AddIgnoredActor(this);
    if (GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetActorLocation(),GetActorLocation(),ECC_Visibility,Params)) return false;
    if (bBrushTool)
    {
        if (Hero->EquippedBrush) return false;
        EquippedBy=Hero; Hero->EquippedBrush=this; Phase=EMCFoodPhase::Equipped; OnRep_Phase();
        Hero->ForceNetUpdate(); ForceNetUpdate(); return true;
    }
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
    if (IsValid(EquippedBy)) { EquippedBy->EquippedBrush=nullptr; EquippedBy->ForceNetUpdate(); } EquippedBy=nullptr;
    Phase=EMCFoodPhase::Disposed; OnRep_Phase(); ForceNetUpdate(); SetLifeSpan(3);
}
void AMCFoodActor::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority())
    {
        for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]);
        if (IsValid(EquippedBy) && EquippedBy->EquippedBrush==this) EquippedBy->EquippedBrush=nullptr;
    }
    Super::EndPlay(Reason);
}
void AMCFoodActor::OnHit(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent* OtherComponent,FVector,const FHitResult& Hit)
{
    if (!HasAuthority() || IsDisposed() || !IsValid(Other)) return;
    if (Phase==EMCFoodPhase::Falling && Hit.ImpactNormal.Z>.6f && OtherComponent && OtherComponent->GetCollisionObjectType()==ECC_WorldStatic)
        bLandingPending=true;
    UMCToothStatusComponent* Target=Other->FindComponentByClass<UMCToothStatusComponent>();
    if (!Target || !Target->IsAlive() || Phase==EMCFoodPhase::Stuck || bBrushTool) return;
    if (const auto* Arena=Cast<AMCArenaTooth>(Other); Arena && !Arena->IsAvailable()) return;
    if (const auto* Hero=Cast<AMCToothCharacter>(Other); Hero && Holders.Contains(Hero)) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if (const double* Prev=LastHit.Find(Other); Prev && Now-*Prev<Settings.HitCooldown) return;
    // A player running into stationary food must not turn their own speed (or the
    // solver's separation impulse) into a new attack. Require the food's incoming
    // motion before collision resolution, then check that the two bodies approach.
    const FVector Approach=-Hit.ImpactNormal.GetSafeNormal();
    const float IncomingSpeed=FVector::DotProduct(PrePhysicsVelocity,Approach);
    if (!FMath::IsFinite(IncomingSpeed) || IncomingSpeed<Settings.ImpactSpeed) return;
    const float Speed=FVector::DotProduct(PrePhysicsVelocity-Other->GetVelocity(),Approach);
    if (!FMath::IsFinite(Speed)) return;
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
    if (!HasAuthority() && bReceivedMotion)
    {
        const float Alpha=1-FMath::Exp(-25.f*Dt);
        SetActorLocationAndRotation(FMath::Lerp(GetActorLocation(),NetworkLocation,Alpha),FQuat::Slerp(GetActorQuat(),NetworkRotation,Alpha),false,nullptr,ETeleportType::TeleportPhysics);
    }
    if (HasAuthority() && !IsDisposed())
    {
        if (Phase==EMCFoodPhase::Equipped) { if (IsValid(EquippedBy)) SetActorLocation(EquippedBy->GetActorLocation()); return; }
        if (Phase==EMCFoodPhase::Stuck && IsValid(StuckTooth))
            if (const auto* Tooth=Cast<AMCArenaTooth>(StuckTooth); !Tooth || !Tooth->IsAvailable()) { Phase=EMCFoodPhase::Free; OnRep_Phase(); }
        if (!bSpoiled && SpoilAt>0 && GetWorld()->GetTimeSeconds()>=SpoilAt) Spoil();
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
            // Fixed strength per team: unlike multiplying by mass, heavy food really resists pulling.
            Body->AddForce(Acceleration*9.f*FMath::Sqrt(float(Holders.Num())));
        }
        // An escaped item returns to the arena, never counts as successfully disposed.
        // A placed brush bin owns the horizontal exit. A fixed X cutoff would
        // delete tools inside an artist arena whose front edge moved.
        if (bBrushTool && GetActorLocation().Z < -250) { Dispose(); return; }
        if (GetActorLocation().Z<-250)
        { for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]); SetActorLocation(FVector(0,0,Settings.DropHeight),false,nullptr,ETeleportType::TeleportPhysics); Body->SetPhysicsLinearVelocity(FVector::ZeroVector); }
        PrePhysicsVelocity=Body->GetPhysicsLinearVelocity();
    }
    FString Caption=Phase==EMCFoodPhase::Stuck?FString::Printf(TEXT("E + MOVE TO CENTRE\nPULL %.0f%% | %d GRIPS"),PullProgress*100,Holders.Num()):TEXT("HOLD E: DRAG TO THROAT");
    if (!ItemName.IsNone()) Caption=FString::Printf(TEXT("%s | HP %.0f | %.1f kg\n%s | %s"),*FoodData.Label.ToString(),Health,Settings.Mass,*Caption,bSpoiled?TEXT("INFECTED"):*FString::Printf(TEXT("SPOIL %.0fs"),FMath::Max(0.,SpoilAt-(GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds()))));
    if (bBrushTool) Caption=TEXT("E: PICK UP BRUSH\nQ: THROW OVERBOARD");
    Label->SetText(FText::FromString(Caption));
    if (const auto* PC=GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        Label->SetWorldRotation((PC->PlayerCameraManager->GetCameraLocation()-Label->GetComponentLocation()).Rotation());
}
void AMCFoodActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCFoodActor,Settings); DOREPLIFETIME(AMCFoodActor,Phase);
    DOREPLIFETIME(AMCFoodActor,PullProgress); DOREPLIFETIME(AMCFoodActor,PullDirection); DOREPLIFETIME(AMCFoodActor,Holders);
    DOREPLIFETIME(AMCFoodActor,ItemMesh); DOREPLIFETIME(AMCFoodActor,FoodData); DOREPLIFETIME(AMCFoodActor,ItemName); DOREPLIFETIME(AMCFoodActor,Health);
    DOREPLIFETIME(AMCFoodActor,bFragment); DOREPLIFETIME(AMCFoodActor,bBrushTool); DOREPLIFETIME(AMCFoodActor,bSpoiled); DOREPLIFETIME(AMCFoodActor,SpoilAt);
    DOREPLIFETIME(AMCFoodActor,Batch); DOREPLIFETIME(AMCFoodActor,EquippedBy); DOREPLIFETIME(AMCFoodActor,StuckTooth);
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
    Super::Tick(Dt);
    Label->SetText(FText::FromString(bBrushBin?TEXT("<<< BRUSHES OVERBOARD\nTHROW Q HERE"):TEXT("FOOD >>> THROAT\nNO BRUSHES")));
    if (bBrushBin && !bExitConfigured)
    {
        bExitConfigured=true;
        // The blockout has an invisible front containment wall. Only tools pass it;
        // never disable collision on visible, authored mouth geometry or on the floor.
        FHitResult Hit; const FVector P=GetActorLocation();
        if (GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(250,0,0),P-FVector(100,0,0),ECC_WorldStatic)
            && Hit.GetActor() && Hit.GetActor()->IsHidden() && Hit.GetComponent()
            && Hit.GetComponent()->Bounds.BoxExtent.X<80 && Hit.GetComponent()->Bounds.BoxExtent.Z>150)
            Hit.GetComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Ignore);
    }
    if (!HasAuthority()) return;
    for (TActorIterator<AMCFoodActor> It(GetWorld());It;++It)
        if (It->Phase==EMCFoodPhase::Free && It->bBrushTool==bBrushBin && Volume->Bounds.GetBox().IsInside(It->GetActorLocation())) It->Dispose();
}
void AMCFoodDisposal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMCFoodDisposal,bBrushBin); }

void AMCFoodActor::OnRep_Item()
{
    if (ItemMesh) { Visual->SetStaticMesh(ItemMesh); Visual->SetRelativeLocation(FVector::ZeroVector); Visual->SetRelativeScale3D(FVector(bFragment?.5f:1.f)); }
    if (bBrushTool) { Body->SetCollisionObjectType(ECC_GameTraceChannel1); Body->SetBoxExtent(FVector(12,12,40)); Visual->SetRelativeLocation(FVector(0,0,-35)); Visual->SetRelativeScale3D(FVector(.8)); }
    else if (!ItemName.IsNone()) Body->SetBoxExtent(FoodData.HalfExtent*(bFragment?.5f:1.f));
}
void AMCFoodActor::ConfigureItem(FName Name,const FMCFoodRow& Row,FRandomStream& Random,bool Fragment)
{
    if (!HasAuthority()) return;
    FoodData=Row; FoodData.Sanitize(); ItemName=Name; bFragment=Fragment;
    if (bFragment) { FoodData.Mass/=FoodData.Fragments; FoodData.Health=25; }
    Health=FoodData.Health; Settings.Mass=FoodData.Mass;
    const auto& Choices=bFragment?FoodData.FragmentMeshes:FoodData.WholeMeshes;
    if (!Choices.IsEmpty()) ItemMesh=Choices[Random.RandRange(0,Choices.Num()-1)].LoadSynchronous();
    OnRep_Item(); ForceNetUpdate();
}
void AMCFoodActor::ConfigureBrush()
{
    bBrushTool=true; ItemMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Art/Meshes/SM_Brush.SM_Brush")); Settings.Mass=1; OnRep_Item();
}
float AMCFoodActor::DragSpeed() const
{
    return Phase==EMCFoodPhase::Stuck?55.f:FMath::Clamp(440.f/(1+Settings.Mass/(9.f*FMath::Max(1,Holders.Num()))),70.f,330.f);
}
void AMCFoodActor::Throw(AMCToothCharacter* Hero)
{
    if (!HasAuthority() || !IsValid(Hero) || (EquippedBy!=Hero && !Holders.Contains(Hero))) return;
    const bool WasTool=bBrushTool && EquippedBy==Hero;
    for (int32 I=Holders.Num()-1;I>=0;--I) Release(Holders[I]);
    if (WasTool) { Hero->EquippedBrush=nullptr; EquippedBy=nullptr; SetActorLocation(Hero->GetActorLocation()+Hero->GetActorForwardVector()*70+FVector(0,0,30)); }
    if (Phase!=EMCFoodPhase::Stuck) { Phase=EMCFoodPhase::Free; OnRep_Phase(); Body->SetPhysicsLinearVelocity(Hero->GetActorForwardVector()*(WasTool?1000.f:550.f)+FVector(0,0,250)); }
    Hero->ForceNetUpdate(); ForceNetUpdate();
}
bool AMCFoodActor::HitFood(float Damage,FVector Direction)
{
    if (!HasAuthority() || bBrushTool || IsDisposed() || Phase==EMCFoodPhase::Stuck || !FMath::IsFinite(Damage) || Damage<=0 || Direction.ContainsNaN()) return false;
    Health=FMath::Max(0.f,Health-Damage); ForceNetUpdate();
    if (Health>0 || bFragment) { Body->AddImpulse(Direction.GetSafeNormal()*150+FVector(0,0,60),NAME_None,true); return true; }
    FRandomStream Random(FMath::Rand()); const FVector P=GetActorLocation();
    for (int32 I=0;I<FoodData.Fragments;++I)
    {
        const float Angle=I*2*PI/FoodData.Fragments; const FVector Offset(FMath::Cos(Angle)*42,FMath::Sin(Angle)*42,20);
        const FTransform T(P+Offset);
        auto* Part=GetWorld()->SpawnActorDeferred<AMCFoodActor>(StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Part) continue;
        Part->ConfigureItem(ItemName,FoodData,Random,true); Part->Batch=Batch; Part->SpoilAt=SpoilAt; Part->bSpoiled=bSpoiled;
        UGameplayStatics::FinishSpawningActor(Part,T); Part->Body->SetPhysicsLinearVelocity(Offset*3);
    }
    Dispose(); return true;
}
void AMCFoodActor::Spoil()
{
    // One lesion per piece; bounded population keeps a neglected meal inexpensive.
    int32 Count=0; for (TActorIterator<AMCMouthSurface> It(GetWorld());It;++It) if (It->bUlcer) ++Count;
    if (Count>=24) { bSpoiled=true; return; }
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MCFoodRot),false,this);
    if (!GetWorld()->LineTraceSingleByChannel(Hit,GetActorLocation(),GetActorLocation()-FVector(0,0,500),ECC_WorldStatic,Params)) return;
    auto* Patch=GetWorld()->SpawnActor<AMCMouthSurface>(Hit.ImpactPoint+Hit.ImpactNormal*5,FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator());
    if (Patch)
    {
        Patch->bUlcer=true;
        if (const auto* GS=GetWorld()->GetGameState<AMCGameState>(); GS && GS->DayPlan)
        { Patch->HealSeconds=GS->DayPlan->UlcerHealSeconds; Patch->DamagePerSecond=GS->DayPlan->UlcerDamagePerSecond; Patch->DisturbDamage=GS->DayPlan->UlcerDisturbDamage; }
        Patch->ForceNetUpdate(); bSpoiled=true; ForceNetUpdate();
    }
}
